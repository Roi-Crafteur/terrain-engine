// The planet level of a world on disk (planet_file.hpp).
#include "terrain/storage/planet_file.hpp"

#include "byte_stream.hpp"
#include "file_commit.hpp"
#include "storage_codec.hpp"
#include "terrain/storage/world_meta.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terrain {

namespace {

using storage::ByteReader;
using storage::ByteWriter;

constexpr storage::Magic kMagic = {'T', 'P', 'L', 'N'};

// Every parameter of the level but its threads, in a fixed order: the level it builds.
void writeParams(ByteWriter& out, const PlanetParams& p) {
    out.f64(p.cellSize);
    out.i32(p.samplesPerAxis);
    out.i32(p.floorSamples);
    out.f64(p.minOceanArea);
    out.u8(p.breachSills ? 1 : 0);
    for (const double v : {p.breach.depthLow, p.breach.depthHigh, p.breach.areaLow,
                           p.breach.areaHigh, p.breach.mountainDepth}) {
        out.f64(v);
    }
    const PlanetPassParams& passes = p.passes;
    if (passes.measured) {
        throw std::invalid_argument("planet file: a trial over a part of the world is never written");
    }
    out.u8(passes.enabled ? 1 : 0);
    for (const double v : {passes.mountainCut, passes.plainCut, passes.margin,
                           passes.sectionStep, passes.lateralStep}) {
        out.f64(v);
    }
    out.i32(passes.halfWidth);
    const PlanetClimateParams& c = p.climate;
    for (const float v : {c.equatorTemperature, c.poleTemperature, c.lapseRate, c.iceTemperature}) {
        out.f32(v);
    }
    for (const double v :
         {c.tradeMeridional, c.westerlyMeridional, c.polarMeridional, c.coastPrecipitation,
          c.moistureGrowth, c.rainLength, c.evaporationLength, c.upliftHeight, c.airDescentLength,
          c.orographyRadius, c.capacityFactor, c.recycling, c.eddyRadius, c.eddyShare,
          c.eddyHeight, c.itcz, c.subtropicalDip, c.polarFront, c.minRainFactor, c.petRate,
          c.petZero, c.aridityLow, c.aridityHigh}) {
        out.f64(v);
    }
    out.i32(c.blurPasses);
    const PlanetRiverParams& r = p.rivers;
    for (const double v :
         {r.minDischarge, r.widthCoefficient, r.depthCoefficient, r.depthExponent}) {
        out.f64(v);
    }
    const PlanetFeatureParams& f = p.features;
    for (const double v : {f.deltaMinDischarge, f.deltaMaxSlope, f.shelfDepth,
                           f.estuaryMinDischarge, f.fallMinDischarge, f.fallMinDrop,
                           f.fallMinSlope, f.fallBreak, f.gorgeMinDischarge, f.gorgeMinDepth}) {
        out.f64(v);
    }
    out.i32(f.mouthReach);
}

std::vector<std::uint8_t> paramsBytes(const PlanetParams& p) {
    ByteWriter out;
    writeParams(out, p);
    return out.take();
}

void writeCells(ByteWriter& out, const PlanetCells& c) {
    out.i32(c.grid.nx);
    out.i32(c.grid.nz);
    out.f64(c.grid.cellSize);
    out.count(c.rowTemperature.size());
    for (const float t : c.rowTemperature) {
        out.f32(t);
    }
    out.count(c.elevation.size());
    for (std::size_t i = 0; i < c.elevation.size(); ++i) {
        out.u16(static_cast<std::uint16_t>(c.elevation[i]));
        out.u16(c.precipitation[i]);
        out.u8(c.bits[i]);
    }
    out.count(c.lakeCells.size());
    for (const PlanetCells::LakeCell& l : c.lakeCells) {
        out.u32(l.cell);
        out.i32(l.lake);
    }
    out.count(c.riverCells.size());
    for (const PlanetCells::RiverCell& r : c.riverCells) {
        out.u32(r.cell);
        out.i32(r.segment);
        out.i32(r.vertex);
    }
}

PlanetCells readCells(ByteReader& in) {
    PlanetCells c;
    c.grid.nx = in.i32();
    c.grid.nz = in.i32();
    c.grid.cellSize = in.f64();
    c.rowTemperature.resize(in.count(4));
    for (float& t : c.rowTemperature) {
        t = in.f32();
    }
    const std::size_t cells = in.count(5);
    if (c.grid.nx <= 0 || c.grid.nz <= 0 || cells != c.grid.cells() ||
        c.rowTemperature.size() != static_cast<std::size_t>(c.grid.nz)) {
        in.fail("cells that do not fill their grid");
    }
    c.elevation.resize(cells);
    c.precipitation.resize(cells);
    c.bits.resize(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        c.elevation[i] = static_cast<std::int16_t>(in.u16());
        c.precipitation[i] = in.u16();
        c.bits[i] = in.u8();
    }
    c.lakeCells.resize(in.count(8));
    for (PlanetCells::LakeCell& l : c.lakeCells) {
        l.cell = in.u32();
        l.lake = in.i32();
    }
    c.riverCells.resize(in.count(12));
    for (PlanetCells::RiverCell& r : c.riverCells) {
        r.cell = in.u32();
        r.segment = in.i32();
        r.vertex = in.i32();
    }
    return c;
}

void writeLakes(ByteWriter& out, const std::vector<PlanetLake>& lakes) {
    out.count(lakes.size());
    for (const PlanetLake& l : lakes) {
        out.i32(l.node);
        out.u8(static_cast<std::uint8_t>((l.overflowing ? 1 : 0) | (l.playa ? 2 : 0) |
                                         (l.frozen ? 4 : 0)));
        out.f32(l.level);
        out.u32(l.pit);
        out.u32(l.pass);
        out.u32(l.partner);
        out.u32(l.cells);
        out.f64(l.volume);
        out.i64(l.inflow);
        out.i64(l.evaporation);
        out.i64(l.outflow);
    }
}

std::vector<PlanetLake> readLakes(ByteReader& in) {
    std::vector<PlanetLake> lakes(in.count(57));
    for (PlanetLake& l : lakes) {
        l.node = in.i32();
        const std::uint8_t flags = in.u8();
        l.overflowing = (flags & 1U) != 0;
        l.playa = (flags & 2U) != 0;
        l.frozen = (flags & 4U) != 0;
        l.level = in.f32();
        l.pit = in.u32();
        l.pass = in.u32();
        l.partner = in.u32();
        l.cells = in.u32();
        l.volume = in.f64();
        l.inflow = in.i64();
        l.evaporation = in.i64();
        l.outflow = in.i64();
    }
    return lakes;
}

void writeIndices(ByteWriter& out, const std::vector<std::int32_t>& v) {
    out.count(v.size());
    for (const std::int32_t i : v) {
        out.i32(i);
    }
}

std::vector<std::int32_t> readIndices(ByteReader& in) {
    std::vector<std::int32_t> v(in.count(4));
    for (std::int32_t& i : v) {
        i = in.i32();
    }
    return v;
}

void writeRivers(ByteWriter& out, const RiverGraph& g) {
    out.count(g.segments.size());
    for (const RiverSegment& s : g.segments) {
        out.count(s.vertices.size());
        for (const RiverVertex& v : s.vertices) {
            out.f64(v.x);
            out.f64(v.z);
            out.f32(v.elevation);
            out.f32(v.discharge);
            out.f32(v.width);
            out.f32(v.depth);
            out.u32(v.cell);
        }
        out.u8(s.strahler);
        out.u8(static_cast<std::uint8_t>(s.mouth));
        out.i32(s.downstream);
        out.i32(s.mouthLake);
        writeIndices(out, s.upstream);
        writeIndices(out, s.sourceLakes);
        out.f64(s.length);
    }
    out.count(g.lakeStrahler.size());
    for (const std::uint8_t o : g.lakeStrahler) {
        out.u8(o);
    }
}

RiverGraph readRivers(ByteReader& in) {
    RiverGraph g;
    g.segments.resize(in.count(30));
    for (RiverSegment& s : g.segments) {
        s.vertices.resize(in.count(36));
        for (RiverVertex& v : s.vertices) {
            v.x = in.f64();
            v.z = in.f64();
            v.elevation = in.f32();
            v.discharge = in.f32();
            v.width = in.f32();
            v.depth = in.f32();
            v.cell = in.u32();
        }
        s.strahler = in.u8();
        const std::uint8_t mouth = in.u8();
        if (mouth > static_cast<std::uint8_t>(RiverMouth::Ice)) {
            in.fail("unknown river mouth");
        }
        s.mouth = static_cast<RiverMouth>(mouth);
        s.downstream = in.i32();
        s.mouthLake = in.i32();
        s.upstream = readIndices(in);
        s.sourceLakes = readIndices(in);
        s.length = in.f64();
    }
    g.lakeStrahler.resize(in.count(1));
    for (std::uint8_t& o : g.lakeStrahler) {
        o = in.u8();
    }
    return g;
}

void writeFeatures(ByteWriter& out, const std::vector<RiverFeature>& features) {
    out.count(features.size());
    for (const RiverFeature& f : features) {
        out.u8(static_cast<std::uint8_t>(f.kind));
        out.i32(f.segment);
        out.i32(f.firstVertex);
        out.i32(f.lastVertex);
        out.f64(f.x);
        out.f64(f.z);
        out.f32(f.discharge);
        out.f32(f.magnitude);
    }
}

std::vector<RiverFeature> readFeatures(ByteReader& in) {
    std::vector<RiverFeature> features(in.count(37));
    for (RiverFeature& f : features) {
        const std::uint8_t kind = in.u8();
        if (kind > static_cast<std::uint8_t>(RiverFeatureKind::Gorge)) {
            in.fail("unknown river feature");
        }
        f.kind = static_cast<RiverFeatureKind>(kind);
        f.segment = in.i32();
        f.firstVertex = in.i32();
        f.lastVertex = in.i32();
        f.x = in.f64();
        f.z = in.f64();
        f.discharge = in.f32();
        f.magnitude = in.f32();
    }
    return features;
}

void writeTotals(ByteWriter& out, const PlanetLevel& level) {
    const PlanetWaterBudget& b = level.budget();
    out.i64(b.runoff);
    out.i64(b.toOcean);
    out.i64(b.lakeEvaporation);
    const PlanetTimings& t = level.timings();
    for (const double v : {t.relief, t.ocean, t.climate, t.order, t.hierarchy, t.lakes, t.flow,
                           t.rivers, t.features, t.breach, t.passes}) {
        out.f64(v);
    }
    const PlanetBreach& s = level.breach();
    out.u64(s.depressions);
    out.u64(s.breached);
    out.f64(s.breachedArea);
    out.f64(s.keptArea);
    out.u64(s.cutCells);
    out.f32(s.deepestCut);
    const PlanetPasses& passes = level.passes();
    out.u64(passes.edges);
    out.u64(passes.ladders);
    out.u64(passes.held);
    out.f32(passes.highest);
}

void readTotals(ByteReader& in, PlanetLevel::Parts& p) {
    p.budget = {in.i64(), in.i64(), in.i64()};
    PlanetTimings& t = p.timings;
    for (double* v : {&t.relief, &t.ocean, &t.climate, &t.order, &t.hierarchy, &t.lakes, &t.flow,
                      &t.rivers, &t.features, &t.breach, &t.passes}) {
        *v = in.f64();
    }
    PlanetBreach& s = p.breach;
    s.depressions = static_cast<std::size_t>(in.u64());
    s.breached = static_cast<std::size_t>(in.u64());
    s.breachedArea = in.f64();
    s.keptArea = in.f64();
    s.cutCells = static_cast<std::size_t>(in.u64());
    s.deepestCut = in.f32();
    PlanetPasses& passes = p.passes;
    passes.edges = static_cast<std::size_t>(in.u64());
    passes.ladders = static_cast<std::size_t>(in.u64());
    passes.held = static_cast<std::size_t>(in.u64());
    passes.highest = in.f32();
}

// What the file says it holds: the level of seed on world, with these parameters, this
// generator; otherwise stale.
void checkOrigin(ByteReader& in, std::uint64_t seed, const PlanetParams& params,
                 const WorldSize& world) {
    const std::uint64_t fileSeed = in.u64();
    const std::uint32_t generator = in.u32();
    const double x = in.f64();
    const double z = in.f64();
    const std::size_t size = in.count(1);
    const std::span<const std::uint8_t> stored = in.take(size);
    const std::vector<std::uint8_t> wanted = paramsBytes(params);
    if (fileSeed != seed || generator != kGeneratorVersion || x != world.x || z != world.z ||
        !std::equal(stored.begin(), stored.end(), wanted.begin(), wanted.end())) {
        in.fail("stale: the planet level of another seed, world, generator or parameters");
    }
}

} // namespace

void savePlanetFile(const std::filesystem::path& file, const PlanetLevel& level,
                    std::uint64_t seed, const WorldSize& world) {
    ByteWriter out = storage::beginFrame(kMagic, kPlanetFileVersion, level.memoryBytes());
    out.u64(seed);
    out.u32(kGeneratorVersion);
    out.f64(world.x);
    out.f64(world.z);
    const std::vector<std::uint8_t> params = paramsBytes(level.params());
    out.count(params.size());
    out.bytes(params);
    out.u64(level.checksum());
    writeCells(out, level.cells());
    writeLakes(out, level.lakes());
    writeRivers(out, level.rivers());
    writeFeatures(out, level.features());
    writeTotals(out, level);
    const std::array<storage::FileWrite, 1> writes = {
        storage::FileWrite{file, storage::sealFrame(std::move(out))}};
    storage::commitFiles(writes, {}, {.keepBackups = false, .createDirectories = true});
}

PlanetLevel loadPlanetFile(const std::filesystem::path& file, std::uint64_t seed,
                           const PlanetParams& params, const WorldSize& world) {
    const std::vector<std::uint8_t> bytes = storage::readFileBytes(file);
    const std::string what = storage::pathText(file);
    const storage::Frame frame = storage::openFrame(bytes, kMagic, kPlanetFileVersion, what);
    ByteReader in(frame.body, what + ": ");
    checkOrigin(in, seed, params, world);
    const std::uint64_t checksum = in.u64();
    PlanetLevel::Parts parts;
    parts.params = params;
    parts.cells = readCells(in);
    parts.lakes = readLakes(in);
    parts.rivers = readRivers(in);
    parts.features = readFeatures(in);
    readTotals(in, parts);
    in.expectEnd("bytes after the level");
    PlanetLevel level(std::move(parts));
    if (level.checksum() != checksum) {
        throw StorageError(StorageErrorKind::Corrupt, what + ": a level unlike its checksum");
    }
    return level;
}

PlanetLevel openPlanetLevel(const std::filesystem::path& file, std::uint64_t seed,
                            const HeightfieldParams& heightfield, const PlanetParams& params,
                            const WorldSize& world, bool* built) {
    bool newer = false;
    try {
        PlanetLevel level = loadPlanetFile(file, seed, params, world);
        if (built != nullptr) {
            *built = false;
        }
        return level;
    } catch (const StorageError& e) {
        // Missing, damaged or stale: built again. A file of a newer version is never replaced.
        newer = e.kind() == StorageErrorKind::NewerVersion;
    }
    PlanetLevel level(seed, heightfield, params, world);
    if (!newer) {
        try {
            savePlanetFile(file, level, seed, world);
        } catch (const StorageError&) {
            // Kept in memory only.
        }
    }
    if (built != nullptr) {
        *built = true;
    }
    return level;
}

} // namespace terrain
