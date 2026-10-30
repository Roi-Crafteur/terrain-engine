#include "terrain/generation/settlement/settlement_maps.hpp"

#include "../planet/planet_parallel.hpp"
#include "analysis_cell.hpp"
#include "analysis_distance.hpp"
#include "map_influences.hpp"
#include "map_inputs.hpp"
#include "settlement_hash.hpp"
#include "settlement_random.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terrain::settlement {

namespace {

// PlanetLevel::humidityAt reads the evapotranspiration as at least this.
constexpr double kLeastPet = 300.0;
// The Mana of the craters and of the volcanoes: the major craters and the large volcanoes in full,
// the others in part.
constexpr double kMinorCrater = 0.7;
constexpr double kSmallVolcano = 0.6;

using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point since) {
    return std::chrono::duration<double>(Clock::now() - since).count();
}

// The points of the geology and of the magic, each in its field.
struct PointFields {
    PointField nodes;
    PointField craters;
    PointField deadZones;
    PointField volcanoes;
    PointField arcs;
    PointField active;
};

PointFields pointFields(const SettlementMaps& m, const SettlementConfig& config) {
    const double px = m.grid.periodX();
    const double pz = m.grid.periodZ();
    const GeologyRule& g = config.geology;
    const MagicRule& magic = config.magic;
    const double influence = g.volcanoInfluenceKm * 1000.0;
    PointFields f{PointField(px, pz, magic.nodeRadiusKm.max * 1000.0),
             PointField(px, pz, std::max(g.craterRadiusKm.max, g.majorCraterRadiusKm.max) * 1000.0),
             PointField(px, pz, magic.deadZoneRadiusKm.max * 1000.0),
             PointField(px, pz, influence), PointField(px, pz, influence),
             PointField(px, pz, config.danger.volcanoRadiusKm * 1000.0)};
    for (const ManaNode& n : m.network.nodes) {
        f.nodes.add(n.x, n.z, n.radiusKm * 1000.0, n.intensity);
    }
    for (const ArcaneCrater& c : m.geology.craters) {
        f.craters.add(c.x, c.z, c.radiusKm * 1000.0, c.major ? 1.0 : kMinorCrater);
    }
    for (const DeadZone& d : m.network.deadZones) {
        f.deadZones.add(d.x, d.z, d.radiusKm * 1000.0, 1.0);
    }
    for (const Volcano& v : m.geology.volcanoes) {
        const double intensity = v.large ? 1.0 : kSmallVolcano;
        f.volcanoes.add(v.x, v.z, influence, intensity);
        if (v.kind == VolcanoKind::Arc) {
            f.arcs.add(v.x, v.z, influence, intensity);
        }
        if (v.active) {
            f.active.add(v.x, v.z, config.danger.volcanoRadiusKm * 1000.0, 1.0);
        }
    }
    return f;
}

// The 4 points of the relief a cell samples for its roughness, at kSampleOffset of its side from
// its center.
constexpr double kSampleOffset = 0.3;

// What the pass over the cells reads.
struct Context {
    const PlanetLevel& level;
    const Heightfield* field;
    const Geography* geography;
    const SettlementData& data;
    const PointFields& fields;
    const planet::LandValues<std::uint8_t>& lines;
    std::vector<noise::FbmOctaves> deposits;
};

// The range of the 4 heights of each cell of row j (0 without a relief), by column.
std::vector<double> sampleRanges(const Context& ctx, const SettlementMaps& m, int j) {
    std::vector<double> ranges(static_cast<std::size_t>(m.grid.nx), 0.0);
    if (ctx.field == nullptr) {
        return ranges;
    }
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<int> columns;
    const double q = kSampleOffset * m.grid.cellSize;
    for (int i = 0; i < m.grid.nx; ++i) {
        if (!m.land->land(m.grid.index(i, j))) {
            continue;
        }
        columns.push_back(i);
        for (int k = 0; k < 4; ++k) {
            xs.push_back(m.grid.centerX(i) + ((k & 1) != 0 ? q : -q));
            zs.push_back(m.grid.centerZ(j) + ((k & 2) != 0 ? q : -q));
        }
    }
    std::vector<float> heights(xs.size());
    ctx.field->heightsAt(xs, zs, heights);
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const float* h = heights.data() + 4 * c;
        ranges[static_cast<std::size_t>(columns[c])] =
            std::max({h[0], h[1], h[2], h[3]}) - std::min({h[0], h[1], h[2], h[3]});
    }
    return ranges;
}

CellInputs gatherInputs(const Context& ctx, const SettlementMaps& m, int i, int j) {
    const PlanetGrid& grid = m.grid;
    const std::size_t cell = grid.index(i, j);
    CellInputs in;
    in.criteria = m.criteria(ctx.level, cell);
    in.water = m.water[cell];
    float low = ctx.level.elevation(cell);
    float high = low;
    for (int k = 0; k < 8; ++k) {
        const std::size_t n = grid.neighbor(cell, k);
        if (!m.land->land(n)) {
            continue;
        }
        low = std::min(low, ctx.level.elevation(n));
        high = std::max(high, ctx.level.elevation(n));
        in.riverNear = std::max(in.riverNear, m.water[n] & kRiverClassMask);
        in.saltNear = in.saltNear || (m.water[n] & kSaltLakeBit) != 0;
    }
    in.riverNear = std::max(in.riverNear, in.water & kRiverClassMask);
    in.floorRange = high - low;
    const double x = grid.centerX(i);
    const double z = grid.centerZ(j);
    if (ctx.geography != nullptr) {
        const GeographySample g = ctx.geography->sampleAt(x, z);
        in.relief = g.relief;
        in.belt = g.belt;
        in.character = g.character;
    }
    in.lines = unquantize(ctx.lines[cell]);
    in.nodes = ctx.fields.nodes.at(x, z);
    in.craters = ctx.fields.craters.at(x, z);
    in.deadZones = ctx.fields.deadZones.at(x, z);
    in.volcanoes = ctx.fields.volcanoes.at(x, z);
    in.arcVolcanoes = ctx.fields.arcs.at(x, z);
    in.activeVolcanoes = ctx.fields.active.at(x, z);
    in.fault = unquantize(m.geology.fault[cell]);
    in.majorFault = unquantize(m.geology.majorFault[cell]);
    in.divergence = unquantize(m.geology.divergence[cell]);
    return in;
}

void scoreRow(const Context& ctx, SettlementMaps& m, int j) {
    const std::vector<double> ranges = sampleRanges(ctx, m, j);
    for (int i = 0; i < m.grid.nx; ++i) {
        const std::size_t cell = m.grid.index(i, j);
        if (!m.land->land(cell)) {
            continue;
        }
        CellInputs in = gatherInputs(ctx, m, i, j);
        in.sampleRange = ranges[static_cast<std::size_t>(i)];
        const CellScores s = scoreCell(ctx.data.config, in);
        m.relief.at(cell) = quantize(in.relief);
        m.roughness.at(cell) = quantize(s.roughness);
        m.vegetation.at(cell) = quantize(s.vegetation);
        m.terrainModel.at(cell) = static_cast<std::uint8_t>(s.model);
        m.arable.at(cell) = quantize(s.arable);
        m.food.at(cell) = quantize(s.food);
        m.danger.at(cell) = quantize(s.danger);
        m.prana.at(cell) = quantize(s.prana);
        m.mana.at(cell) = quantize(s.mana);
        m.habitability.at(cell) = quantize(s.habitability);
        const double x = m.grid.centerX(i);
        const double z = m.grid.centerZ(j);
        for (std::size_t r = 0; r < ctx.data.resources.size(); ++r) {
            const double deposit = 0.5 * (ctx.deposits[r].fbm2D(x, z) + 1.0);
            m.resources[r].at(cell) =
                quantize(resourceValue(ctx.data.resources[r], in, s, deposit));
        }
    }
}

void distances(const PlanetLevel& level, SettlementMaps& m, int threads) {
    std::vector<std::uint8_t> columns(m.grid.cells(), 1);
    m.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        const std::uint8_t w = m.water.values()[slot];
        if ((w & kRiverClassMask) != 0 || (w & kFreshLakeBit) != 0) {
            columns[cell] = 0;
        }
    });
    columnDistances(m.grid, columns, threads);
    m.waterKm = landDistances(m.grid, m.land, columns, threads);
    for (std::size_t c = 0; c < columns.size(); ++c) {
        columns[c] = level.cells().ocean(c) ? 0 : 1;
    }
    columnDistances(m.grid, columns, threads);
    m.coastKm = landDistances(m.grid, m.land, columns, threads);
}

planet::LandValues<std::uint8_t> zeros(const std::shared_ptr<const planet::LandIndex>& land) {
    return planet::LandValues<std::uint8_t>(land, 0, 0);
}

} // namespace

double cellHumidity(const PlanetLevel& level, std::size_t cell) {
    const double p = level.precipitation(cell);
    const double pet = std::max(
        planet::potentialEvapotranspiration(level.temperature(cell), level.params().climate),
        kLeastPet);
    return p / (p + pet);
}

CriterionValues SettlementMaps::criteria(const PlanetLevel& level, std::size_t cell) const {
    CriterionValues c{};
    c[static_cast<std::size_t>(Criterion::Altitude)] = level.elevation(cell);
    c[static_cast<std::size_t>(Criterion::Temperature)] = level.temperature(cell);
    c[static_cast<std::size_t>(Criterion::Rainfall)] = level.precipitation(cell);
    c[static_cast<std::size_t>(Criterion::Humidity)] = cellHumidity(level, cell);
    c[static_cast<std::size_t>(Criterion::Roughness)] = unquantize(roughness[cell]);
    c[static_cast<std::size_t>(Criterion::WaterDistance)] = waterKm[cell];
    c[static_cast<std::size_t>(Criterion::CoastDistance)] = coastKm[cell];
    c[static_cast<std::size_t>(Criterion::Latitude)] = planet::rowLatitude(grid, grid.row(cell));
    return c;
}

std::size_t SettlementMaps::cellAt(double x, double z) const {
    return planetCellAt(grid, x, z);
}

std::size_t SettlementMaps::memoryBytes() const {
    std::size_t bytes = land->memoryBytes() + geology.memoryBytes() + network.memoryBytes();
    for (const auto* map : {&water, &relief, &roughness, &waterKm, &coastKm, &vegetation,
                            &terrainModel, &arable, &food, &danger, &prana, &telluric, &mana,
                            &habitability}) {
        bytes += map->values().capacity();
    }
    for (const auto& r : resources) {
        bytes += r.values().capacity();
    }
    return bytes;
}

std::uint64_t SettlementMaps::checksum() const {
    Fnv64 f;
    for (const auto* map : {&water, &relief, &roughness, &waterKm, &coastKm, &vegetation,
                            &terrainModel, &arable, &food, &danger, &prana, &telluric, &mana,
                            &habitability, &geology.fault, &geology.majorFault, &geology.divergence}) {
        f.addBytes(map->values());
    }
    for (const auto& r : resources) {
        f.addBytes(r.values());
    }
    for (const Volcano& v : geology.volcanoes) {
        f.add(v.x);
        f.add(v.z);
        f.add(static_cast<std::uint64_t>(v.kind) | (v.active ? 16u : 0u) | (v.large ? 32u : 0u), 1);
    }
    for (const ArcaneCrater& c : geology.craters) {
        f.add(c.x);
        f.add(c.z);
        f.add(c.radiusKm);
    }
    for (const TelluricLine& l : network.lines) {
        f.add(l.x.back());
        f.add(l.z.back());
        f.add(l.widthKm);
    }
    for (const ManaNode& n : network.nodes) {
        f.add(n.x);
        f.add(n.z);
        f.add(n.intensity);
    }
    for (const DeadZone& d : network.deadZones) {
        f.add(d.x);
        f.add(d.radiusKm);
    }
    return f.value();
}

SettlementMaps computeSettlementMaps(const PlanetLevel& level, const Heightfield* field,
                                     const SettlementData& data, std::uint64_t worldSeed,
                                     int threads) {
    const int workers = planet::resolveThreads(threads);
    const Geography* geography = field != nullptr ? field->geography() : nullptr;
    SettlementMaps m;
    m.grid = level.grid();
    Clock::time_point start = Clock::now();
    m.land = landOf(level.cells());
    m.water = waterFlags(level, m.land, data.config.rivers, workers);
    m.timings.inputs = seconds(start);
    start = Clock::now();
    distances(level, m, workers);
    m.timings.distances = seconds(start);
    start = Clock::now();
    if (geography != nullptr) {
        m.geology = buildGeology(m.grid, m.land, *geography, worldSeed, data.config, workers);
    } else {
        m.geology.fault = zeros(m.land);
        m.geology.majorFault = zeros(m.land);
        m.geology.divergence = zeros(m.land);
    }
    m.timings.geology = seconds(start);
    start = Clock::now();
    const WorldSize world{m.grid.periodX(), m.grid.periodZ()};
    m.network = buildTelluricNetwork(world, worldSeed, data.config.magic,
                                     worldScale(m.grid, data.config));
    m.telluric = stampLines(m.grid, m.land, m.network);
    m.timings.network = seconds(start);
    start = Clock::now();
    for (auto* map : {&m.relief, &m.roughness, &m.vegetation, &m.terrainModel, &m.arable, &m.food,
                      &m.danger, &m.prana, &m.mana, &m.habitability}) {
        *map = zeros(m.land);
    }
    m.resources.assign(data.resources.size(), zeros(m.land));
    const PointFields fields = pointFields(m, data.config);
    Context ctx{level, field, geography, data, fields, m.telluric, {}};
    for (const ResourceDef& r : data.resources) {
        ctx.deposits.emplace_back(streamOf(worldSeed, "deposit:" + r.id),
                                  noise::FbmParams{.octaves = 2,
                                                   .frequency = 1.0 / (r.depositScaleKm * 1000.0),
                                                   .gain = 0.5,
                                                   .lattices = noise::OctaveLattices::Unrotated},
                                  world);
    }
    planet::parallelRows(m.grid.nz, workers, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            scoreRow(ctx, m, j);
        }
    });
    m.timings.cells = seconds(start);
    return m;
}

} // namespace terrain::settlement
