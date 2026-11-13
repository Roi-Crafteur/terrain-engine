#pragma once

// The small world with its water and the checks of the water level of its outputs against the
// core (task 7b), for water_level_test.

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace water_level {

using terrain::HeightSample;
using terrain::kNoWaterLevel;
using terrain::TileData;
using terrain::WaterKind;

inline constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

inline int threads() { return std::max(1, static_cast<int>(std::thread::hardware_concurrency())); }

inline terrain::HorizonParams horizonParams() {
    terrain::HorizonParams params;
    params.cellsPerSide = 64;
    params.world = golden::kSmallPlanet;
    return params;
}

// The small world with its water, its tiles (with and without caves) and its horizon.
struct World {
    terrain::Heightfield relief{kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet};
    std::shared_ptr<const terrain::PlanetLevel> planet = std::make_shared<terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads())),
        golden::smallPlanetParams(threads()));
    std::shared_ptr<const terrain::Hydrology> hydrology =
        std::make_shared<const terrain::Hydrology>(kSeed, relief, planet);
    terrain::TerrainTiles tiles{kSeed, terrain::HeightfieldParams{}, terrain::EcologyParams{},
                                std::nullopt, golden::kSmallPlanet, hydrology};
    terrain::TerrainTiles caveTiles{kSeed, terrain::HeightfieldParams{}, terrain::EcologyParams{},
                                    terrain::CaveParams{}, golden::kSmallPlanet, hydrology};
    terrain::HorizonSampler horizon{tiles.heightfield(), tiles.ecology(), horizonParams()};

    [[nodiscard]] const terrain::Heightfield& field() const { return tiles.heightfield(); }
};

inline bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
inline bool wet(float level) { return level > kNoWaterLevel; }

// The level the outputs give at a point: HeightSample::waterLevel under water, else none.
inline float coreLevel(const terrain::Heightfield& field, double x, double z) {
    const HeightSample s = field.sampleAt(x, z);
    return s.water == WaterKind::None ? kNoWaterLevel : s.waterLevel;
}

inline bool underLake(WaterKind kind) {
    return kind == WaterKind::Lake || kind == WaterKind::SaltLake;
}

inline TileData tileAt(const terrain::TerrainTiles& tiles, double x, double z, int lod) {
    x = terrain::wrapCoordinate(x, golden::kSmallPlanet.x);
    z = terrain::wrapCoordinate(z, golden::kSmallPlanet.z);
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    return tiles.generate({terrain::tileContaining(chunk, lod), 0});
}

// The level of every vertex of a tile against the core, and its checksum.
struct Checks {
    std::size_t columns = 0; // vertices on a column
    std::size_t wet = 0;     // of them under water
    std::size_t edges = 0;   // vertices between two columns
    std::size_t wrong = 0;
    golden::Checksum sum;
};

inline void checkVertex(const World& w, const TileData& t, std::size_t n, Checks& c) {
    const double spacing = terrain::tileCellMeters(t.request.key.lod);
    const terrain::Vec3f v = t.mesh.vertices[n];
    const double u = v.x / spacing;
    const double q = v.z / spacing;
    const float level = t.waterLevels[n];
    if (u == std::floor(u) && q == std::floor(q)) {
        ++c.columns;
        c.wet += wet(level) ? 1u : 0u;
        c.wrong += same(level, coreLevel(w.field(), t.originX + v.x, t.originZ + v.z)) ? 0u : 1u;
        return;
    }
    ++c.edges;
    float low = 0.0f;
    float high = kNoWaterLevel;
    for (const double cu : {std::floor(u), std::ceil(u)}) {
        for (const double cq : {std::floor(q), std::ceil(q)}) {
            const float column = coreLevel(w.field(), t.originX + cu * spacing,
                                           t.originZ + cq * spacing);
            if (wet(column)) {
                low = wet(high) ? std::min(low, column) : column;
                high = std::max(high, column);
            }
        }
    }
    c.wrong += (wet(high) ? (level >= low && level <= high) : !wet(level)) ? 0u : 1u;
}

inline void checkTile(const World& w, const TileData& t, Checks& c) {
    TEST_CHECK(t.waterLevels.size() == t.mesh.vertices.size());
    for (std::size_t n = 0; n < t.mesh.vertices.size() && n < t.waterLevels.size(); ++n) {
        checkVertex(w, t, n, c);
        c.sum.add(t.waterLevels[n]);
    }
}

// The samples of a rectangle of a level around (x, z), each against the core.
inline std::vector<terrain::HorizonCell> checkHorizon(const World& w, int level, double x,
                                                      double z, int half, Checks& c) {
    const terrain::HorizonParams& params = w.horizon.params();
    const terrain::HorizonIndex center = terrain::canonicalCenter(params, level, x, z);
    const terrain::HorizonRect rect{center.x - half, center.z - half, center.x + half + 1,
                                    center.z + half + 1};
    const std::vector<terrain::HorizonCell> cells = w.horizon.sample(level, rect);
    const terrain::HorizonIndex counts = terrain::levelCounts(params, level);
    const int pitch = terrain::levelPitch(params, level);
    std::size_t next = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            const terrain::HorizonCell& cell = cells[next++];
            const terrain::HorizonIndex s = terrain::canonicalSample({i, k}, counts);
            const float core = coreLevel(w.field(), terrain::samplePosition(s.x, pitch),
                                         terrain::samplePosition(s.z, pitch));
            ++c.columns;
            c.wet += wet(cell.waterLevel) ? 1u : 0u;
            c.wrong += same(cell.waterLevel, core) ? 0u : 1u;
            c.wrong += ((cell.flags & terrain::kHorizonWater) != 0) == wet(core) ? 0u : 1u;
            c.sum.add(cell.waterLevel);
        }
    }
    return cells;
}

inline void report(const char* what, const Checks& c) {
    std::printf("%s: %zu on columns or samples (%zu under water), %zu between columns, %zu wrong\n",
                what, c.columns, c.wet, c.edges, c.wrong);
    TEST_CHECK(c.wrong == 0 && c.columns > 0 && c.wet > 0);
}

} // namespace water_level
