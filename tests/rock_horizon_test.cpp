// The rock of the streamed world against the horizon (phase 12A): the tiles of the outer ring of
// the selection meet level 0 of the horizon, which samples heightAt every 8 m and joins its
// samples straight. Around the highest wall by the K2, where faces stand at the outer sides of the
// LOD 3 tiles, those sides keep the relief alone (TileRequest::horizon): every vertex of an outer
// side lies on the border of the horizon there, as on the relief without rock.
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "seam_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::TileRequest;

// The foot of the highest wall by the K2 (mountain_freeze_test's window "highest wall").
constexpr double kWallX = 416'017.0;
constexpr double kWallZ = -11'414'806.0;
// The view of a client in Unreal: tiles to 53 chunks (about 850 m).
constexpr int kViewDistance = 53;
constexpr double kHorizonPitch = 8.0; // meters between the samples of level 0 of the horizon

struct Gap {
    double largest = 0.0;
    std::size_t points = 0;
    std::size_t sides = 0;
};

// The border of the horizon on the vertical plane of a side of a tile: heightAt every 8 m.
std::vector<seam_test::Segment> horizonBorder(const terrain::Heightfield& field,
                                              seam_test::Plane plane, double from, double to) {
    std::vector<seam_test::Segment> border;
    const auto at = [&](double u) {
        const double x = plane.alongX ? plane.at : u;
        const double z = plane.alongX ? u : plane.at;
        return seam_test::FacePoint{u, static_cast<double>(field.heightAt(x, z))};
    };
    for (double u = from; u < to; u += kHorizonPitch) {
        border.emplace_back(at(u), at(u + kHorizonPitch));
    }
    return border;
}

// The largest distance from a vertex of an outer side of a tile of the outer ring (no tile of the
// selection beyond it) to the border of the horizon there.
Gap outerGap(const terrain::TerrainTiles& tiles, ChunkCoord player) {
    terrain::LodParams lod;
    lod.viewDistance = kViewDistance;
    const std::vector<TileKey> selection = terrain::selectTiles(player, lod);
    std::set<std::pair<int, int>> covered;
    for (const TileKey& key : selection) {
        const int side = terrain::tileChunks(key.lod);
        for (int dz = 0; dz < side; ++dz) {
            for (int dx = 0; dx < side; ++dx) {
                covered.insert({key.origin.x + dx, key.origin.z + dz});
            }
        }
    }
    Gap gap;
    for (const TileRequest& request : terrain::tileRequests(selection)) {
        const TileKey& key = request.key;
        const int side = terrain::tileChunks(key.lod);
        const double meters = side * terrain::kChunkSizeMeters;
        const double x0 = key.origin.x * terrain::kChunkSizeMeters;
        const double z0 = key.origin.z * terrain::kChunkSizeMeters;
        // The outer sides: -x, +x, -z, +z.
        const std::pair<int, int> beyond[4] = {{key.origin.x - 1, key.origin.z},
                                               {key.origin.x + side, key.origin.z},
                                               {key.origin.x, key.origin.z - 1},
                                               {key.origin.x, key.origin.z + side}};
        const seam_test::Plane planes[4] = {
            {true, x0}, {true, x0 + meters}, {false, z0}, {false, z0 + meters}};
        std::optional<terrain::TileData> data;
        for (int s = 0; s < 4; ++s) {
            if (covered.count(beyond[s]) != 0) {
                continue;
            }
            if (!data) {
                data = tiles.generate(request);
            }
            const double from = planes[s].alongX ? z0 : x0;
            const std::vector<seam_test::Segment> border =
                horizonBorder(tiles.heightfield(), planes[s], from, from + meters);
            for (const seam_test::FacePoint p : seam_test::facePoints(*data, planes[s])) {
                gap.largest = std::max(gap.largest, seam_test::distanceToOutline(p, border));
                ++gap.points;
            }
            ++gap.sides;
        }
    }
    return gap;
}

void testOuterSides() {
    const terrain::TerrainTiles rock(terrain::kDefaultWorldSeed);
    const terrain::TerrainTiles relief(terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {},
                                       nullptr, false);
    const ChunkCoord player = terrain::worldToChunk(kWallX, kWallZ);
    const Gap withRock = outerGap(rock, player);
    const Gap alone = outerGap(relief, player);
    std::printf("outer sides by the K2: %zu sides, %zu vertices; largest distance to the horizon "
                "%.4f m (relief alone %.4f m)\n",
                withRock.sides, withRock.points, withRock.largest, alone.largest);
    TEST_CHECK(withRock.sides > 40 && withRock.points > 1'000);
    TEST_CHECK(alone.largest < 1e-3);
    TEST_CHECK(withRock.largest < 1e-3);
}

} // namespace

int main() {
    testOuterSides();
    return test::result();
}
