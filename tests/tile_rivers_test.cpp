// The water of the columns of the tiles and their falls (TileData::waterColumns, TileData::falls,
// agent 27) on the small world with its water: the kind and the level of every column are those
// of the core bit for bit, two tiles of a LOD hold the same columns on their common side, the flow
// of a river goes down its course, every fall lies in the one tile of each LOD that holds its lip
// with its foot downstream at the water below, the same data whatever the thread or the order,
// and nothing on dry land or without a hydrology.
#include "water_level_support.hpp"

#include <array>
#include <cmath>
#include <thread>

namespace {

using terrain::TileData;
using terrain::WaterColumn;
using terrain::WaterKind;
using water_level::same;
using water_level::tileAt;
using water_level::World;

constexpr int kSide = terrain::kGridSamples;

bool inland(WaterKind kind) {
    return kind == WaterKind::River || kind == WaterKind::Lake || kind == WaterKind::SaltLake;
}

const WaterColumn& columnAt(const TileData& t, int i, int k) {
    return t.waterColumns[static_cast<std::size_t>(k * kSide + i)];
}

// The falls of 4 m and more of the first reaches that have some.
std::vector<terrain::RiverFall> someFalls(const World& w, std::size_t most) {
    std::vector<terrain::RiverFall> falls;
    for (std::size_t r = 0; r < w.hydrology->reachCount() && falls.size() < most; ++r) {
        for (const terrain::RiverFall& f : w.hydrology->reach(r).falls) {
            if (falls.size() < most && f.top - f.bottom >= 4.0f) {
                falls.push_back(f);
            }
        }
    }
    return falls;
}

// Every column of a tile against the core: its kind and its level bit for bit.
bool columnsMatch(const World& w, const TileData& t) {
    if (t.waterColumns.size() != static_cast<std::size_t>(kSide * kSide)) {
        return false;
    }
    const double spacing = terrain::tileCellMeters(t.request.key.lod);
    bool match = true;
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            const WaterColumn& c = columnAt(t, i, k);
            const terrain::HeightSample s =
                w.field().sampleAt(t.originX + i * spacing, t.originZ + k * spacing);
            const float level = s.water == WaterKind::None ? terrain::kNoWaterLevel : s.waterLevel;
            match = match && c.kind == s.water && same(c.level, level);
            const bool flows = c.flowX != 0.0f || c.flowZ != 0.0f;
            match = match && (c.kind == WaterKind::River || !flows);
        }
    }
    return match;
}

// The tiles of every LOD at the lips of the falls and their neighbors along x and z: the columns
// against the core, the common sides, the falls in the one tile holding their lip.
void testTiles(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::size_t tiles = 0;
    std::size_t sides = 0;
    std::size_t held = 0;
    bool match = true;
    bool shared = true;
    bool once = true;
    for (const terrain::RiverFall& f : falls) {
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const double side = terrain::tileSizeMeters(lod);
            const TileData t = tileAt(w.tiles, f.x, f.z, lod);
            const TileData east = tileAt(w.tiles, t.originX + side, t.originZ, lod);
            const TileData south = tileAt(w.tiles, t.originX, t.originZ + side, lod);
            match = match && columnsMatch(w, t);
            ++tiles;
            for (int j = 0; j < kSide; ++j) {
                if (!east.waterColumns.empty()) {
                    shared = shared && columnAt(t, kSide - 1, j) == columnAt(east, 0, j);
                    ++sides;
                }
                if (!south.waterColumns.empty()) {
                    shared = shared && columnAt(t, j, kSide - 1) == columnAt(south, j, 0);
                    ++sides;
                }
            }
            const double lipX = terrain::wrapCoordinate(f.x, golden::kSmallPlanet.x);
            const double lipZ = terrain::wrapCoordinate(f.z, golden::kSmallPlanet.z);
            std::size_t found = 0;
            for (const TileData* d : {&t, &east, &south}) {
                for (const terrain::TileFall& g : d->falls) {
                    found += std::abs(g.x - lipX) < 1e-6 && std::abs(g.z - lipZ) < 1e-6 ? 1u : 0u;
                }
            }
            once = once && found == 1;
            held += found;
        }
    }
    TEST_CHECK(match && shared && once && sides > 0);
    std::printf("tiles: %zu at the lips of %zu falls, columns %s, %zu columns of common sides %s, "
                "%zu falls %s\n",
                tiles, falls.size(), match ? "those of the core" : "NOT THOSE OF THE CORE", sides,
                shared ? "the same" : "DIFFERENT", held,
                once ? "each in the one tile of its lip" : "NOT IN THE TILE OF THEIR LIP");
}

// The flow of the river columns of the LOD 0 tiles around the falls: a unit vector, and the water
// never higher a few meters downstream than as far upstream.
void testFlow(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::size_t rivers = 0;
    std::size_t pairs = 0;
    std::size_t down = 0;
    std::size_t up = 0;
    bool unit = true;
    for (const terrain::RiverFall& f : falls) {
        const TileData t = tileAt(w.tiles, f.x, f.z, 0);
        for (std::size_t n = 0; n < t.waterColumns.size(); ++n) {
            const WaterColumn& c = t.waterColumns[n];
            if (c.kind != WaterKind::River) {
                continue;
            }
            ++rivers;
            unit = unit && std::abs(std::hypot(c.flowX, c.flowZ) - 1.0) < 1e-5;
            const double x = t.originX + static_cast<double>(n % kSide);
            const double z = t.originZ + static_cast<double>(n / kSide);
            const terrain::HeightSample a = w.field().sampleAt(x - 3.0 * c.flowX, z - 3.0 * c.flowZ);
            const terrain::HeightSample b = w.field().sampleAt(x + 3.0 * c.flowX, z + 3.0 * c.flowZ);
            if (a.water == WaterKind::River && b.water == WaterKind::River) {
                ++pairs;
                down += b.waterLevel < a.waterLevel ? 1u : 0u;
                up += b.waterLevel > a.waterLevel + 0.01f ? 1u : 0u;
            }
        }
    }
    TEST_CHECK(unit && pairs > 0 && down > pairs / 4 && up * 100 <= pairs);
    std::printf("flow: %zu river columns, %s; down the flow over 6 m: %zu pairs, %zu lower, %zu "
                "higher\n",
                rivers, unit ? "unit vectors" : "NOT UNIT VECTORS", pairs, down, up);
}

// The foot of each fall lies downstream of its lip, at the water below.
void testFeet(const World& w, const std::vector<terrain::RiverFall>& falls) {
    bool feet = true;
    double worst = 0.0;
    for (const terrain::RiverFall& f : falls) {
        const double run = std::hypot(f.footX - f.x, f.footZ - f.z);
        const terrain::HeightSample s = w.field().sampleAt(f.footX, f.footZ);
        feet = feet && run > 0.0 && run < 2000.0;
        if (s.water == WaterKind::River) {
            worst = std::max(worst, std::abs(static_cast<double>(s.waterLevel - f.bottom)));
        }
    }
    TEST_CHECK(feet && worst < 0.5);
    std::printf("feet: %s, at most %.3f m from the water below\n",
                feet ? "downstream of their lips" : "NOT DOWNSTREAM", worst);
}

// The same water data generated alone, and by two threads in opposite orders.
void testThreads(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::vector<TileData> alone;
    for (const terrain::RiverFall& f : falls) {
        alone.push_back(tileAt(w.tiles, f.x, f.z, 1));
    }
    std::vector<TileData> forward(falls.size());
    std::vector<TileData> backward(falls.size());
    std::thread a([&] {
        for (std::size_t n = 0; n < falls.size(); ++n) {
            forward[n] = tileAt(w.tiles, falls[n].x, falls[n].z, 1);
        }
    });
    std::thread b([&] {
        for (std::size_t n = falls.size(); n-- > 0;) {
            backward[n] = tileAt(w.tiles, falls[n].x, falls[n].z, 1);
        }
    });
    a.join();
    b.join();
    bool same = true;
    for (std::size_t n = 0; n < falls.size(); ++n) {
        same = same && alone[n].waterColumns == forward[n].waterColumns &&
               alone[n].waterColumns == backward[n].waterColumns &&
               alone[n].falls == forward[n].falls && alone[n].falls == backward[n].falls;
    }
    TEST_CHECK(same);
    std::printf("threads: %s\n", same ? "the same water data" : "DIFFERENT WATER DATA");
}

// No water data on dry land, nor without a hydrology.
void testDry(const World& w, const std::vector<terrain::RiverFall>& falls) {
    std::size_t dry = 0;
    bool none = true;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 8; ++i) {
            const double x = (i + 0.37) * golden::kSmallPlanet.x / 8.0;
            const double z = (j + 0.61) * golden::kSmallPlanet.z / 4.0;
            const TileData t = tileAt(w.tiles, x, z, 3);
            const double spacing = terrain::tileCellMeters(3);
            bool inlandWater = false;
            for (int k = 0; k < kSide; ++k) {
                for (int c = 0; c < kSide; ++c) {
                    inlandWater = inlandWater ||
                                  inland(w.field()
                                             .sampleAt(t.originX + c * spacing,
                                                       t.originZ + k * spacing)
                                             .water);
                }
            }
            if (!inlandWater) {
                ++dry;
                none = none && t.waterColumns.empty() && t.falls.empty();
            }
        }
    }
    const terrain::TerrainTiles relief(water_level::kSeed, terrain::HeightfieldParams{},
                                       terrain::EcologyParams{}, std::nullopt,
                                       golden::kSmallPlanet);
    for (const terrain::RiverFall& f : falls) {
        const TileData t = tileAt(relief, f.x, f.z, 0);
        none = none && t.waterColumns.empty() && t.falls.empty();
    }
    TEST_CHECK(none && dry > 0);
    std::printf("dry: %zu tiles without inland water and the tiles without a hydrology, %s\n", dry,
                none ? "no water data" : "SOME WATER DATA");
}

} // namespace

int main() {
    const World w;
    const std::vector<terrain::RiverFall> falls = someFalls(w, 6);
    TEST_CHECK(falls.size() == 6);
    testTiles(w, falls);
    testFlow(w, falls);
    testFeet(w, falls);
    testThreads(w, falls);
    testDry(w, falls);
    return test::result();
}
