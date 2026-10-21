#include "terrain/generation/planet/planet_level.hpp"

#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <utility>

namespace terrain {

namespace {

// 64-bit FNV-1a over the little-endian bytes of the values.
class Fnv {
public:
    void add(std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            hash_ = (hash_ ^ ((value >> (8 * i)) & 0xFFu)) * 0x100000001B3ULL;
        }
    }
    void add(float v) { add(std::bit_cast<std::uint32_t>(v), 4); }
    void add(double v) { add(std::bit_cast<std::uint64_t>(v), 8); }
    void add(std::int32_t v) { add(static_cast<std::uint32_t>(v), 4); }
    void add(std::uint32_t v) { add(static_cast<std::uint64_t>(v), 4); }
    void add(std::int64_t v) { add(static_cast<std::uint64_t>(v), 8); }
    void add(std::uint8_t v) { add(static_cast<std::uint64_t>(v), 1); }
    void add(bool v) { add(static_cast<std::uint64_t>(v ? 1 : 0), 1); }
    template <class T> void addAll(const std::vector<T>& values) {
        add(static_cast<std::uint64_t>(values.size()), 8);
        for (const T& v : values) {
            add(v);
        }
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

void addCells(Fnv& f, const PlanetCells& cells) {
    f.addAll(cells.rowTemperature);
    f.add(static_cast<std::uint64_t>(cells.elevation.size()), 8);
    for (const std::int16_t e : cells.elevation) {
        f.add(static_cast<std::uint64_t>(static_cast<std::uint16_t>(e)), 2);
    }
    for (const std::uint16_t p : cells.precipitation) {
        f.add(static_cast<std::uint64_t>(p), 2);
    }
    f.addAll(cells.bits);
    for (const PlanetCells::LakeCell& l : cells.lakeCells) {
        f.add(l.cell);
        f.add(l.lake);
    }
    for (const PlanetCells::RiverCell& r : cells.riverCells) {
        f.add(r.cell);
        f.add(r.segment);
        f.add(r.vertex);
    }
}

void addLakes(Fnv& f, const std::vector<PlanetLake>& lakes) {
    f.add(static_cast<std::uint64_t>(lakes.size()), 8);
    for (const PlanetLake& l : lakes) {
        f.add(l.node);
        f.add(l.overflowing);
        f.add(l.playa);
        f.add(l.frozen);
        f.add(l.level);
        f.add(l.pit);
        f.add(l.pass);
        f.add(l.partner);
        f.add(l.cells);
        f.add(l.volume);
        f.add(l.inflow);
        f.add(l.evaporation);
        f.add(l.outflow);
    }
}

void addRivers(Fnv& f, const RiverGraph& graph, const std::vector<RiverFeature>& features) {
    f.add(static_cast<std::uint64_t>(graph.segments.size()), 8);
    for (const RiverSegment& s : graph.segments) {
        f.add(static_cast<std::uint64_t>(s.vertices.size()), 8);
        for (const RiverVertex& v : s.vertices) {
            f.add(v.x);
            f.add(v.z);
            f.add(v.elevation);
            f.add(v.discharge);
            f.add(v.width);
            f.add(v.depth);
            f.add(v.cell);
        }
        f.add(s.strahler);
        f.add(static_cast<std::uint8_t>(s.mouth));
        f.add(s.downstream);
        f.add(s.mouthLake);
        f.addAll(s.upstream);
        f.addAll(s.sourceLakes);
        f.add(s.length);
    }
    f.addAll(graph.lakeStrahler);
    f.add(static_cast<std::uint64_t>(features.size()), 8);
    for (const RiverFeature& r : features) {
        f.add(static_cast<std::uint8_t>(r.kind));
        f.add(r.segment);
        f.add(r.firstVertex);
        f.add(r.lastVertex);
        f.add(r.x);
        f.add(r.z);
        f.add(r.discharge);
        f.add(r.magnitude);
    }
}

template <class T> std::size_t bytesOf(const std::vector<T>& v) {
    return v.capacity() * sizeof(T);
}

} // namespace

PlanetLevel::PlanetLevel(std::uint64_t seed, const HeightfieldParams& heightfield,
                         const PlanetParams& params, const WorldSize& world)
    : PlanetLevel(planet::buildPlanet(Heightfield(seed, heightfield, world), params), params) {}

PlanetLevel::PlanetLevel(PlanetBuild&& build, const PlanetParams& params)
    : params_(params), cells_(planet::compactCells(build)), lakes_(std::move(build.lakes)),
      rivers_(std::move(build.rivers)), features_(std::move(build.features)), budget_(build.budget),
      timings_(build.timings) {
    build = PlanetBuild{}; // the full-precision cells are not kept
}

float PlanetLevel::temperature(std::size_t cell) const {
    return planet::cellTemperature(
        cells_.rowTemperature[static_cast<std::size_t>(grid().row(cell))], elevation(cell),
        params_.climate);
}

planet::CellWater PlanetLevel::water(std::size_t cell) const {
    return planet::cellWater(
        grid(), cells_.rowTemperature[static_cast<std::size_t>(grid().row(cell))], elevation(cell),
        precipitation(cell), cells_.ocean(cell) ? planet::kOcean : planet::kLand, params_.climate);
}

PlanetSample PlanetLevel::sampleAt(double x, double z) const {
    const PlanetBilinear corners = planetBilinear(grid(), x, z);
    std::array<std::array<double, 4>, 6> v{}; // elevation, P, T, PET, runoff, water surface
    for (std::size_t k = 0; k < 4; ++k) {
        const std::size_t c = corners.cells[k];
        const double t = temperature(c);
        const double pet = planet::potentialEvapotranspiration(t, params_.climate);
        const bool frozen = t < params_.climate.iceTemperature;
        const std::int32_t lake = cells_.lakeAt(c);
        v[0][k] = elevation(c);
        v[1][k] = precipitation(c);
        v[2][k] = t;
        v[3][k] = pet;
        v[4][k] = cells_.ocean(c) ? 0.0
                  : frozen        ? v[1][k]
                                  : planet::landRunoff(v[1][k], pet, params_.climate);
        v[5][k] = cells_.ocean(c) ? static_cast<double>(kSeaLevel)
                  : lake >= 0 ? static_cast<double>(lakes_[static_cast<std::size_t>(lake)].level)
                              : v[0][k];
    }
    const auto blend = [&](const std::array<double, 4>& a) {
        return static_cast<float>(corners.blend(a[0], a[1], a[2], a[3]));
    };
    PlanetSample s;
    s.elevation = blend(v[0]);
    s.precipitation = blend(v[1]);
    s.temperature = blend(v[2]);
    s.pet = blend(v[3]);
    s.runoff = blend(v[4]);
    s.waterSurface = blend(v[5]);
    const std::size_t cell = planetCellAt(grid(), x, z);
    s.ocean = cells_.ocean(cell);
    s.frozen = !s.ocean && temperature(cell) < params_.climate.iceTemperature;
    s.lake = cells_.lakeAt(cell);
    if (const PlanetCells::RiverCell* river = cells_.riverAt(cell)) {
        const RiverSegment& segment = rivers_.segments[static_cast<std::size_t>(river->segment)];
        s.discharge = segment.vertices[static_cast<std::size_t>(river->vertex)].discharge;
        s.strahler = segment.strahler;
    }
    s.wind = planet::rowWind(grid(), grid().row(cell), params_.climate);
    return s;
}

std::vector<std::int32_t> PlanetLevel::basins() const {
    std::vector<std::uint8_t> ocean(grid().cells());
    std::vector<std::uint8_t> receiver(grid().cells());
    for (std::size_t c = 0; c < grid().cells(); ++c) {
        ocean[c] = cells_.ocean(c) ? planet::kOcean : planet::kLand;
        receiver[c] = cells_.receiver(c);
    }
    const planet::LakeAt lakeAt = [this](std::size_t c) { return cells_.lakeAt(c); };
    return planet::computeBasins(grid(), ocean, receiver, lakeAt, lakes_);
}

std::uint64_t PlanetLevel::checksum() const {
    Fnv f;
    addCells(f, cells_);
    addLakes(f, lakes_);
    addRivers(f, rivers_, features_);
    f.add(budget_.runoff);
    f.add(budget_.toOcean);
    f.add(budget_.lakeEvaporation);
    return f.value();
}

std::size_t PlanetLevel::memoryBytes() const {
    std::size_t bytes = sizeof(*this) + cells_.memoryBytes() + bytesOf(lakes_) +
                        bytesOf(features_) + bytesOf(rivers_.segments) +
                        bytesOf(rivers_.lakeStrahler);
    for (const RiverSegment& s : rivers_.segments) {
        bytes += bytesOf(s.vertices) + bytesOf(s.upstream) + bytesOf(s.sourceLakes);
    }
    return bytes;
}

} // namespace terrain
