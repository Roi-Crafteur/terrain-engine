// The water level of the outputs (task 7b) on the small world with its water: at the vertices of
// the tiles (TileData::waterLevels) and at the samples of the far horizon (HorizonCell::
// waterLevel). On a column of a tile and at a sample, the level is the waterLevel of the core bit
// for bit under water and kNoWaterLevel on dry land (between two columns, the level of the wet
// ones); it is flat on a lake, never rises down a river and steps down at its falls; with caves,
// the walls under the water have none. A golden checksum keeps the levels on every platform.
#include "water_level_support.hpp"

#include <array>
#include <map>
#include <optional>

namespace {

using terrain::HeightSample;
using terrain::kNoWaterLevel;
using terrain::TileData;
using terrain::WaterKind;
using water_level::Checks;
using water_level::checkHorizon;
using water_level::checkTile;
using water_level::checkVertex;
using water_level::coreLevel;
using water_level::tileAt;
using water_level::wet;
using water_level::World;

// A point under each of a few lakes of the planet level (non playas), sought around its pit.
std::vector<std::array<double, 2>> lakePoints(const World& w, std::size_t most) {
    std::vector<std::array<double, 2>> points;
    const terrain::PlanetGrid& g = w.planet->grid();
    for (std::size_t l = 0; l < w.planet->lakes().size() && points.size() < most; l += 5) {
        const terrain::PlanetLake& lake = w.planet->lakes()[l];
        if (lake.playa) {
            continue;
        }
        const double cx = g.centerX(g.column(lake.pit));
        const double cz = g.centerZ(g.row(lake.pit));
        bool found = false;
        for (int j = -16; j <= 16 && !found; ++j) {
            for (int i = -16; i <= 16 && !found; ++i) {
                const double x = cx + 250.0 * i + 0.5;
                const double z = cz + 250.0 * j + 0.5;
                if (water_level::underLake(w.field().sampleAt(x, z).water)) {
                    points.push_back({x, z});
                    found = true;
                }
            }
        }
    }
    return points;
}

// The lakes are flat: every vertex on a column and every sample under a lake at the level of its
// lake, the same in a tile and in a window of the horizon.
void testLakes(const World& w, Checks& tiles, Checks& horizon) {
    std::size_t vertices = 0;
    std::size_t samples = 0;
    bool flat = true;
    const auto atLake = [&](double x, double z, float level) {
        const HeightSample s = w.field().sampleAt(x, z);
        if (!water_level::underLake(s.water)) {
            return false;
        }
        // Over the relief the lakes were found over (without the coasts, which keep off them).
        flat = flat &&
               level == w.hydrology->lakeAt(x, z, w.hydrology->relief().heightAt(x, z)).level;
        return true;
    };
    for (const auto& [x, z] : lakePoints(w, 3)) {
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const TileData t = tileAt(w.tiles, x, z, lod);
            checkTile(w, t, tiles);
            const double spacing = terrain::tileCellMeters(lod);
            for (std::size_t n = 0; n < t.mesh.vertices.size(); ++n) {
                const terrain::Vec3f v = t.mesh.vertices[n];
                if (std::fmod(v.x, spacing) == 0.0 && std::fmod(v.z, spacing) == 0.0) {
                    vertices += atLake(t.originX + v.x, t.originZ + v.z, t.waterLevels[n]) ? 1 : 0;
                }
            }
        }
        for (int level = 1; level <= 2; ++level) { // lifted to their crests
            checkHorizon(w, level, x, z, 4, horizon);
        }
        const auto cells = checkHorizon(w, 0, x, z, 8, horizon);
        const terrain::HorizonIndex c0 = terrain::canonicalCenter(w.horizon.params(), 0, x, z);
        const int pitch = terrain::levelPitch(w.horizon.params(), 0);
        for (std::size_t n = 0; n < cells.size(); ++n) {
            const auto i = c0.x - 8 + static_cast<std::int64_t>(n % 17);
            const auto k = c0.z - 8 + static_cast<std::int64_t>(n / 17);
            samples += atLake(terrain::samplePosition(i, pitch), terrain::samplePosition(k, pitch),
                              cells[n].waterLevel)
                           ? 1
                           : 0;
        }
    }
    TEST_CHECK(flat && vertices > 0 && samples > 0);
    std::printf("lakes: %zu vertices and %zu samples under them, %s\n", vertices, samples,
                flat ? "each at the level of its lake" : "NOT FLAT");
}

// Points every step meters down the axis of the course of a reach, over its first length
// meters: the channel meanders about it, so some of them lie beside the water.
std::vector<std::array<double, 2>> alongCourse(const terrain::RiverReach& reach, double step,
                                               double length) {
    std::vector<std::array<double, 2>> points;
    double run = 0.0;
    for (std::size_t i = 1; i < reach.points.size() && run < length; ++i) {
        const terrain::RiverPoint& a = reach.points[i - 1];
        const terrain::RiverPoint& b = reach.points[i];
        const double segment = std::hypot(b.x - a.x, b.z - a.z);
        for (double d = 0.0; d < segment && run + d < length; d += step) {
            points.push_back({a.x + (b.x - a.x) * d / segment, a.z + (b.z - a.z) * d / segment});
        }
        run += segment;
    }
    return points;
}

// The LOD 0 tiles of the small world, each generated and checked once.
class TileCache {
public:
    TileCache(const World& w, Checks& checks) : w_(w), checks_(checks) {}

    const TileData& at(double x, double z) {
        x = terrain::wrapCoordinate(x, golden::kSmallPlanet.x);
        z = terrain::wrapCoordinate(z, golden::kSmallPlanet.z);
        const std::array<std::int64_t, 2> key{
            static_cast<std::int64_t>(std::floor(x / terrain::kChunkSizeMeters)),
            static_cast<std::int64_t>(std::floor(z / terrain::kChunkSizeMeters))};
        auto it = tiles_.find(key);
        if (it == tiles_.end()) {
            it = tiles_.emplace(key, tileAt(w_.tiles, x, z, 0)).first;
            checkTile(w_, it->second, checks_);
        }
        return it->second;
    }

    // The level of the vertex on the column nearest (x, z) when that column is under a river.
    std::optional<float> riverLevel(double x, double z) {
        const double cx = std::round(terrain::wrapCoordinate(x, golden::kSmallPlanet.x));
        const double cz = std::round(terrain::wrapCoordinate(z, golden::kSmallPlanet.z));
        if (w_.field().sampleAt(cx, cz).water != WaterKind::River) {
            return std::nullopt;
        }
        const TileData& t = at(cx, cz);
        for (std::size_t n = 0; n < t.mesh.vertices.size(); ++n) {
            const terrain::Vec3f v = t.mesh.vertices[n];
            if (t.originX + v.x == cx && t.originZ + v.z == cz) {
                return t.waterLevels[n];
            }
        }
        return std::nullopt;
    }

private:
    const World& w_;
    Checks& checks_;
    std::map<std::array<std::int64_t, 2>, TileData> tiles_;
};

// The level of the sample of level 0 nearest (x, z) when it is under a river.
std::optional<float> horizonRiverLevel(const World& w, double x, double z, Checks& c) {
    const terrain::HorizonCell cell = checkHorizon(w, 0, x, z, 0, c).front();
    const terrain::HorizonIndex s = terrain::canonicalCenter(w.horizon.params(), 0, x, z);
    const int pitch = terrain::levelPitch(w.horizon.params(), 0);
    const bool river = w.field()
                           .sampleAt(terrain::samplePosition(s.x, pitch),
                                     terrain::samplePosition(s.z, pitch))
                           .water == WaterKind::River;
    return river ? std::optional<float>(cell.waterLevel) : std::nullopt;
}

// The levels met down a course: never rising by more than tolerance, and coming down overall.
struct Descent {
    float tolerance = 0.0f;
    std::size_t levels = 0;
    std::size_t courses = 0;
    std::size_t down = 0; // courses whose last level lies below the first
    bool rises = false;
    std::optional<float> first;
    std::optional<float> last;

    void add(std::optional<float> level) {
        if (level) {
            ++levels;
            rises = rises || (last && *level > *last + tolerance);
            first = first ? first : level;
            last = level;
        }
    }
    void end() {
        ++courses;
        down += first && last && *last < *first ? 1u : 0u;
        first.reset();
        last.reset();
    }
};

// Down reaches without falls, the level of the river never rises and comes down: at the vertices
// of the tiles every 2 m over 300 m, and at the samples of level 0 every 8 m over 3 km of the
// reaches wider than 3 pitches. The sample nearest a point of the axis may lie a few meters
// upstream of the one before, across a bend: there the level may rise by a hair (0.1 mm seen).
void testRivers(const World& w, Checks& tiles, Checks& horizon) {
    TileCache cache(w, tiles);
    Descent near;
    Descent far;
    far.tolerance = 0.01f;
    for (std::size_t r = 0; r < w.hydrology->reachCount() && (near.courses < 8 || far.courses < 4);
         r += 13) {
        const terrain::RiverReach reach = w.hydrology->reach(r);
        if (!reach.falls.empty() || reach.points.size() < 4) {
            continue;
        }
        if (near.courses < 8) {
            for (const auto& [x, z] : alongCourse(reach, 2.0, 300.0)) {
                near.add(cache.riverLevel(x, z));
            }
            near.end();
        }
        if (far.courses < 4 && reach.points.front().width >= 24.0f) {
            for (const auto& [x, z] : alongCourse(reach, 8.0, 3000.0)) {
                far.add(horizonRiverLevel(w, x, z, horizon));
            }
            far.end();
        }
    }
    TEST_CHECK(!near.rises && near.levels > 100 && near.down > 0);
    TEST_CHECK(!far.rises && far.levels > 50 && far.down > 0);
    std::printf("rivers: %zu levels of vertices down %zu courses (%zu come down), %zu of samples "
                "down %zu (%zu come down), %s\n",
                near.levels, near.courses, near.down, far.levels, far.courses, far.down,
                near.rises || far.rises ? "RISING" : "never rising");
}

// The wet levels between the water below and above a fall, within radius of its lip, from
// low to high.
struct Step {
    float low = 0.0f;
    float high = kNoWaterLevel;

    void add(const terrain::RiverFall& f, float level) {
        if (wet(level) && level <= f.top + 1.0f && level >= f.bottom - 1.0f) {
            low = wet(high) ? std::min(low, level) : level;
            high = std::max(high, level);
        }
    }
    [[nodiscard]] bool steps(const terrain::RiverFall& f) const {
        return wet(high) && high - low >= 0.8 * (static_cast<double>(f.top) - f.bottom);
    }
};

// Around the lips of falls of 4 to 60 m, the levels of the vertices of the tiles step down by at
// least 80 % of the drop; around those of 16 m and more on rivers wider than 2 pitches, the
// levels of the samples of level 0.
void testFalls(const World& w, Checks& tiles, Checks& horizon) {
    TileCache cache(w, tiles);
    std::size_t near = 0;
    std::size_t far = 0;
    bool steps = true;
    for (std::size_t r = 0; r < w.hydrology->reachCount() && (near < 6 || far < 3); ++r) {
        for (const terrain::RiverFall& f : w.hydrology->reach(r).falls) {
            const double drop = static_cast<double>(f.top) - f.bottom;
            if (near < 6 && drop >= 4.0 && drop <= 60.0) {
                ++near;
                Step step;
                const double radius = 24.0 + 0.5 * drop;
                for (double dz = -radius; dz <= radius; dz += terrain::kChunkSizeMeters) {
                    for (double dx = -radius; dx <= radius; dx += terrain::kChunkSizeMeters) {
                        for (const float level : cache.at(f.x + dx, f.z + dz).waterLevels) {
                            step.add(f, level);
                        }
                    }
                }
                steps = steps && step.steps(f);
            }
            if (far < 3 && drop >= 16.0 && f.width >= 16.0f) {
                ++far;
                Step step;
                const int half = 4 + static_cast<int>(drop / 16.0);
                for (const auto& cell : checkHorizon(w, 0, f.x, f.z, half, horizon)) {
                    step.add(f, cell.waterLevel);
                }
                steps = steps && step.steps(f);
            }
        }
    }
    TEST_CHECK(steps && near > 0 && far > 0);
    std::printf("falls: %zu in tiles, %zu in the horizon, %s\n", near, far,
                steps ? "their levels step down by 80 % of the drop at least" : "NOT STEPPING");
}

// Out of the water no level: tiles of LOD 0 and 3 and windows of level 0 at places spread over
// the world, wherever every column or sample is dry for the core.
void testDry(const World& w, Checks& tiles, Checks& horizon) {
    std::size_t dryTiles = 0;
    std::size_t dryWindows = 0;
    bool none = true;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 8; ++i) {
            const double x = (i + 0.37) * golden::kSmallPlanet.x / 8.0;
            const double z = (j + 0.61) * golden::kSmallPlanet.z / 4.0;
            for (const int lod : {0, 3}) {
                const TileData t = tileAt(w.tiles, x, z, lod);
                checkTile(w, t, tiles);
                const double spacing = terrain::tileCellMeters(lod);
                bool dry = true;
                for (int k = 0; k <= terrain::kChunkCells && dry; ++k) {
                    for (int c = 0; c <= terrain::kChunkCells && dry; ++c) {
                        dry = !wet(coreLevel(w.field(), t.originX + c * spacing,
                                             t.originZ + k * spacing));
                    }
                }
                if (dry) {
                    ++dryTiles;
                    none = none && std::none_of(t.waterLevels.begin(), t.waterLevels.end(), wet);
                }
            }
            const auto cells = checkHorizon(w, 0, x, z, 4, horizon);
            if (std::all_of(cells.begin(), cells.end(), [](const terrain::HorizonCell& c) {
                    return (c.flags & terrain::kHorizonWater) == 0;
                })) {
                ++dryWindows;
                none = none && std::none_of(cells.begin(), cells.end(),
                                            [](const terrain::HorizonCell& c) {
                                                return wet(c.waterLevel);
                                            });
            }
        }
    }
    TEST_CHECK(none && dryTiles >= 8 && dryWindows >= 4);
    std::printf("dry land: %zu tiles and %zu windows without water, %s\n", dryTiles, dryWindows,
                none ? "no level in them" : "SOME LEVEL IN THEM");
}

// With caves, only the outer surface takes the water of its columns: never the walls of the
// caves under a lake, a river or the sea. Tiles of LOD 0 around the lakes.
void testCaves(const World& w, const std::vector<std::array<double, 2>>& places, Checks& checks) {
    std::size_t walls = 0;
    std::size_t under = 0; // walls under wet columns
    std::size_t wrong = 0;
    for (const auto& [x, z] : places) {
        for (double dz = -16.0; dz <= 16.0; dz += 16.0) {
            for (double dx = -16.0; dx <= 16.0; dx += 16.0) {
                const TileData t = tileAt(w.caveTiles, x + dx, z + dz, 0);
                wrong += t.waterLevels.size() == t.mesh.vertices.size() ? 0u : 1u;
                for (std::size_t n = 0; n < t.materials.size() && n < t.waterLevels.size(); ++n) {
                    checks.sum.add(t.waterLevels[n]);
                    if (t.materials[n] == terrain::CaveMaterial::Surface) {
                        checkVertex(w, t, n, checks);
                        continue;
                    }
                    const terrain::Vec3f v = t.mesh.vertices[n];
                    ++walls;
                    under += wet(coreLevel(w.field(), t.originX + v.x, t.originZ + v.z)) ? 1u : 0u;
                    wrong += wet(t.waterLevels[n]) ? 1u : 0u;
                }
            }
        }
    }
    TEST_CHECK(wrong == 0 && under > 0);
    std::printf("caves: %zu vertices of walls, %zu of them under water, %zu with a level\n", walls,
                under, wrong);
}

} // namespace

int main() {
    const World w;
    Checks tiles;
    Checks horizon;
    testLakes(w, tiles, horizon);
    testRivers(w, tiles, horizon);
    testFalls(w, tiles, horizon);
    testDry(w, tiles, horizon);
    Checks caves;
    testCaves(w, lakePoints(w, 3), caves);
    water_level::report("vertices", tiles);
    water_level::report("samples", horizon);
    water_level::report("vertices with caves", caves);
    golden::report("water levels of the small world",
                   // The coasts (agent 33) change tiles of their band (0x5E8C41F9A71B2235
                   // without them, as the horizon and the caves).
                   {{"tiles", tiles.sum.value(), 0x6DFC682960D825B5ULL},
                    {"horizon", horizon.sum.value(), 0xEF77A2A8E53BB1EBULL},
                    {"tiles with caves", caves.sum.value(), 0x00F88BBE88F2AFC5ULL}});
    return test::result();
}
