// The cost of the grass of the deserts near the water (agent 49): the ecology asks the hydrology
// how far its rivers are only where the climate may make a desert. Timed pair by pair
// (cost_ratio_support.hpp), the same world without the distance (EcologyParams::desertRivers off)
// and with it, on the real planet (the fixture coast_planet):
// - tiles of LOD 0 to 3 along a river of a rocky desert: at most x1.3;
// - tiles away from any desert (a temperate forest, a taiga): the same cost, at most x1.05.
#include "terrain/generation/ecology.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_planet_support.hpp"
#include "cost_ratio_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

struct Place {
    double x;
    double z;
};

// A river of a rocky desert, a temperate forest and a taiga (terrain_biome_places).
constexpr Place kRiver{-29'975'880.0, -5'858'190.0};
constexpr std::array<Place, 2> kOthers = {{{23'304'512.0, -6'898'112.0},
                                           {3'464'512.0, -11'218'112.0}}};

terrain::TerrainTiles tilesWith(bool desertRivers) {
    terrain::EcologyParams ecology;
    ecology.desertRivers = desertRivers;
    // No cache of the columns: every generation computes its tile.
    return terrain::TerrainTiles(terrain::kDefaultWorldSeed, {}, ecology, std::nullopt, {},
                                 coast_planet::hydrology(), false, 0);
}

std::vector<terrain::TileRequest> requestsAt(const Place& p) {
    std::vector<terrain::TileRequest> requests;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        for (int d = -1; d <= 1; ++d) {
            const double offset = d * terrain::tileSizeMeters(lod);
            const terrain::ChunkCoord chunk{
                static_cast<std::int32_t>(std::floor((p.x + offset) / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor(p.z / terrain::kChunkSizeMeters))};
            requests.push_back({terrain::tileContaining(chunk, lod), 0});
        }
    }
    return requests;
}

cost_test::Ratio ratioOf(const std::vector<terrain::TileRequest>& requests, double most) {
    const terrain::TerrainTiles without = tilesWith(false);
    const terrain::TerrainTiles with = tilesWith(true);
    return cost_test::ratioAtMost(
        requests.size(),
        [&](cost_test::Side side, std::size_t item) {
            const terrain::TerrainTiles& tiles = side == cost_test::Side::With ? with : without;
            return cost_test::secondsOf([&] { (void)tiles.generate(requests[item]); });
        },
        most);
}

} // namespace

int main() {
    const cost_test::Ratio river = ratioOf(requestsAt(kRiver), 1.3);
    std::printf("tiles along a river of a rocky desert, with the distance to the rivers: %s\n",
                cost_test::ratioText(river).c_str());
    TEST_CHECK(river.passed());
    std::vector<terrain::TileRequest> others;
    for (const Place& p : kOthers) {
        const std::vector<terrain::TileRequest> here = requestsAt(p);
        others.insert(others.end(), here.begin(), here.end());
    }
    const cost_test::Ratio away = ratioOf(others, 1.05);
    std::printf("tiles of a temperate forest and of a taiga, away from the deserts: %s\n",
                cost_test::ratioText(away).c_str());
    TEST_CHECK(away.passed());
    return test::result();
}
