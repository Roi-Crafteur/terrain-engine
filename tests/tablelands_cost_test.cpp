// The cost of the plateaus (agent 46, tableland_relief.hpp), switched on, measured as
// water_cost_test measures the water (cost_ratio_support.hpp: the best times of the same items
// with and without, pair by pair):
//   - out of their zone, the tiles of LOD 0 to 3 cost what they cost without them (x1.3 at most);
//   - in a deep canyon of a plateau (its walls, its cliffs, its temples), the cost of a tile of
//     LOD 0 to 3 is printed, with its ratio to the same tiles without the plateaus: a goal, never
//     an assertion on a time (time_goal_support.hpp).
#include "cost_ratio_support.hpp"
#include "tablelands_support.hpp"
#include "test_assert.hpp"
#include "time_goal_support.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/tablelands.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct Place {
    double x = 0.0;
    double z = 0.0;
};

// The deep canyon of the views of stop point 2: its floor, and the direction up its wall to its rim
// about 2 km away.
constexpr Place kCanyonFloor{161206.0, -15104664.0};
constexpr double kUpX = 0.832;
constexpr double kUpZ = -0.555;
constexpr double kWall = 2'000.0;
// Places of data/dev/places.json, those out of the zone timed: a river in a plain, the edge of a
// savanna, the wall by the K2, a notched sea cliff, a transverse dune.
constexpr std::array<Place, 5> kCandidates = {{{-241631.0, -86277.0},
                                               {-26927500.0, -270336.0},
                                               {416102.0, -11414891.0},
                                               {429778.8, -855913.3},
                                               {-997282.0, -3555842.0}}};
// Tiles timed: kTiles of each kind, LOD 0 to 3 in turn; out of the zone at most kMost times the
// cost without the plateaus; the goal of a tile of LOD 0 in the canyon, in milliseconds.
constexpr std::size_t kTiles = 16;
constexpr double kMost = 1.3;
constexpr double kCanyonGoal = 4.0;

terrain::TileRequest requestAt(const Place& p, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(p.x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(p.z / terrain::kChunkSizeMeters))};
    return {terrain::tileContaining(chunk, lod), 0};
}

double tileSeconds(const terrain::TerrainTiles& tiles, const terrain::TileRequest& r,
                   double& sum) {
    std::size_t vertices = 0;
    const double seconds =
        cost_test::secondsOf([&] { vertices = tiles.generate(r).mesh.vertices.size(); });
    sum += static_cast<double>(vertices);
    return seconds;
}

// The tiles up the wall of the canyon, from its floor to its rim, LOD 0 to 3 in turn.
std::vector<terrain::TileRequest> canyonTiles() {
    std::vector<terrain::TileRequest> out;
    for (std::size_t t = 0; t < kTiles; ++t) {
        const double d = kWall * static_cast<double>(t) / static_cast<double>(kTiles - 1);
        out.push_back(requestAt({kCanyonFloor.x + kUpX * d, kCanyonFloor.z + kUpZ * d},
                                static_cast<int>(t % 4)));
    }
    return out;
}

// The tiles around the places out of the zone (2 km around them out of it), LOD 0 to 3 in turn.
std::vector<terrain::TileRequest> outsideTiles(const terrain::Heightfield& on) {
    std::vector<Place> places;
    for (const Place& c : kCandidates) {
        bool out = true;
        for (int k = 0; k < 9; ++k) {
            out = out && !on.tablelandAt(c.x + 1'000.0 * (k % 3 - 1), c.z + 1'000.0 * (k / 3 - 1)).any();
        }
        if (out) {
            places.push_back(c);
        }
    }
    TEST_CHECK(places.size() >= 2);
    std::vector<terrain::TileRequest> tiles;
    for (std::size_t t = 0; t < kTiles && !places.empty(); ++t) {
        const Place& c = places[t % places.size()];
        const Place p{c.x + 50.0 * static_cast<double>(t / places.size()), c.z};
        tiles.push_back(requestAt(p, static_cast<int>(t % 4)));
    }
    return tiles;
}

// The best times of the canyon tiles with and without the plateaus, per LOD.
void reportCanyon(const terrain::TerrainTiles& off, const terrain::TerrainTiles& on, double& sum) {
    const std::vector<terrain::TileRequest> tiles = canyonTiles();
    std::array<double, 4> with{};
    std::array<double, 4> without{};
    for (const terrain::TileRequest& r : tiles) { // what is built on first use
        static_cast<void>(tileSeconds(on, r, sum));
        static_cast<void>(tileSeconds(off, r, sum));
    }
    for (int round = 0; round < 3; ++round) {
        for (std::size_t n = 0; n < tiles.size(); ++n) {
            const auto lod = static_cast<std::size_t>(tiles[n].key.lod);
            const double a = tileSeconds(on, tiles[n], sum);
            const double b = tileSeconds(off, tiles[n], sum);
            with[lod] = round == 0 ? a : std::min(with[lod], a);
            without[lod] = round == 0 ? b : std::min(without[lod], b);
        }
    }
    for (std::size_t lod = 0; lod < 4; ++lod) {
        std::printf("canyon tile of LOD %zu: %.2f ms with the plateaus, %.2f ms without (x%.2f)\n",
                    lod, 1000.0 * with[lod], 1000.0 * without[lod], with[lod] / without[lod]);
    }
    time_goal::report("a tile of LOD 0 in a deep canyon", 1000.0 * with[0], kCanyonGoal, "ms");
}

} // namespace

int main() {
    const terrain::TerrainTiles off(terrain::kDefaultWorldSeed);
    const terrain::TerrainTiles on(terrain::kDefaultWorldSeed, tablelands_test::on());
    TEST_CHECK(on.heightfield().tablelandAt(kCanyonFloor.x, kCanyonFloor.z).tableland > 0.0f);
    double sum = 0.0;
    reportCanyon(off, on, sum);
    const std::vector<terrain::TileRequest> outside = outsideTiles(on.heightfield());
    const cost_test::Ratio ratio = cost_test::ratioAtMost(
        outside.size(), [&](cost_test::Side side, std::size_t n) {
            return tileSeconds(side == cost_test::Side::With ? on : off, outside[n], sum);
        },
        kMost);
    std::printf("tiles out of the zone with the plateaus %s\n", cost_test::ratioText(ratio).c_str());
    TEST_CHECK(ratio.passed());
    std::printf("(%.0f vertices in all)\n", sum);
    return time_goal::result(test::result());
}
