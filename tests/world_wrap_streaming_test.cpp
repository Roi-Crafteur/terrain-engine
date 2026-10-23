// The streamed world across the edges of the wrapped world (WorldSize): around a player on an edge
// or a corner, the selection covers every chunk within reach exactly once, with canonical keys,
// the same tiles and seams as in the middle of the world; walking across an edge reloads only
// what one chunk of walk reloads anywhere; tiles on both sides of an edge share their faces, also
// stitched to a coarser tile; edits given at any image are stored in the canonical range and seen
// from both sides of the edge.
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "legacy_relief_support.hpp"
#include "seam_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::StreamManager;
using terrain::StreamParams;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileRequest;
using terrain::WorldSize;

const WorldSize kWorld{};
const ChunkCoord kCount = terrain::worldChunks(kWorld);
const int kEast = kCount.x / 2 - 1; // the last chunk column of the canonical range, x < W / 2
const int kWest = -kCount.x / 2;    // the first one, x >= -W / 2: east of kEast across the edge
const int kNorth = kCount.z / 2 - 1;
const int kSouth = -kCount.z / 2;
// A chunk row crossing the edge x = world.x / 2 at a southern mid-latitude, on land with plants.
constexpr int kRow = -641774;

TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    return data;
}

StreamParams smallParams() {
    StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = 0;
    params.maxLoadsPerUpdate = 100000;
    return params;
}

double center(int chunk) {
    return (chunk + 0.5) * terrain::kChunkSizeMeters;
}

void loadAll(StreamManager& manager, ChunkCoord player) {
    manager.update(center(player.x), center(player.z));
    for (int round = 0; round < 100 && manager.pendingCount() > 0; ++round) {
        manager.waitIdle();
        manager.update(center(player.x), center(player.z));
    }
}

// The selection around a player, each tile as an offset from the player chunk (its nearest
// image), with its seams.
std::set<std::tuple<int, int, int, unsigned>> relativeSelection(const StreamManager& manager) {
    const ChunkCoord player = *manager.playerChunk();
    std::set<std::tuple<int, int, int, unsigned>> tiles;
    for (const TileRequest& request : manager.selection()) {
        const TileKey seen = terrain::nearestTile(request.key, player, kWorld);
        tiles.insert({seen.origin.x - player.x, seen.origin.z - player.z, seen.lod, request.seams});
    }
    return tiles;
}

// Around players on the edges and the corners (given at any image), the keys are canonical and
// distinct, every chunk within reach lies in exactly one tile, and the tiles and seams are those
// around a player in the middle of the world, at the same place on the grid of the LOD 3 tiles.
void testSelectionAcrossTheEdges() {
    const StreamParams params = smallParams();
    StreamManager middle([](const TileRequest& r) { return fakeTile(r); }, params);
    const int reach = params.lod.viewDistance;
    const ChunkCoord players[] = {{kEast, kRow},   {kWest, kRow},         {kEast, kNorth},
                                  {kWest, kSouth}, {kEast + kCount.x, 3}, {5, kSouth - kCount.z}};
    for (const ChunkCoord player : players) {
        StreamManager manager([](const TileRequest& r) { return fakeTile(r); }, params);
        manager.update(center(player.x), center(player.z));
        const ChunkCoord canonical = terrain::wrapChunk(player, kWorld);
        TEST_CHECK(*manager.playerChunk() == canonical);
        std::set<std::tuple<int, int, int>> keys;
        for (const TileRequest& request : manager.selection()) {
            TEST_CHECK(terrain::wrapTile(request.key, kWorld) == request.key);
            keys.insert({request.key.origin.x, request.key.origin.z, request.key.lod});
        }
        TEST_CHECK(keys.size() == manager.selection().size());
        bool once = true;
        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dx = -reach; dx <= reach; ++dx) {
                const ChunkCoord chunk{canonical.x + dx, canonical.z + dz};
                const auto holding = std::count_if(
                    manager.selection().begin(), manager.selection().end(), [&](const auto& r) {
                        return terrain::tileContains(terrain::nearestTile(r.key, canonical, kWorld),
                                                     chunk);
                    });
                once = once && holding == 1;
            }
        }
        // The same place on the grid of the LOD 3 tiles, far from every edge.
        const ChunkCoord far{64 + (canonical.x & 7), 64 + (canonical.z & 7)};
        middle.update(center(far.x), center(far.z));
        TEST_CHECK(once);
        TEST_CHECK(relativeSelection(manager) == relativeSelection(middle));
    }
}

// Walking two chunks east across the edge generates the tiles that the same walk generates in
// the middle of the world, and keeps the data of the others; a key or a chunk given at another
// image finds the loaded tile and invalidates it.
void testCrossingTheEdge() {
    int generated = 0;
    StreamManager manager(
        [&generated](const TileRequest& r) {
            ++generated;
            return fakeTile(r);
        },
        smallParams());
    loadAll(manager, {kEast - 1, kRow});
    std::map<std::tuple<int, int, int>, std::pair<TileRequest, const TileData*>> before;
    for (const TileRequest& request : manager.selection()) {
        before[{request.key.origin.x, request.key.origin.z, request.key.lod}] = {
            request, manager.find(request.key)};
    }
    generated = 0;
    loadAll(manager, {kWest + 1 + kCount.x, kRow}); // two chunks east, given beyond the edge
    const int acrossTheEdge = generated;
    std::size_t kept = 0;
    bool same = true;
    for (const TileRequest& request : manager.selection()) {
        const auto old = before.find({request.key.origin.x, request.key.origin.z, request.key.lod});
        if (old != before.end() && old->second.first == request) {
            same = same && manager.find(request.key) == old->second.second;
            ++kept;
        }
    }
    TEST_CHECK(same && kept > 0);
    TEST_CHECK(manager.loadedCount() == manager.selection().size());

    int middleGenerated = 0;
    StreamManager middle(
        [&middleGenerated](const TileRequest& r) {
            ++middleGenerated;
            return fakeTile(r);
        },
        smallParams());
    loadAll(middle, {62, kRow}); // the same places on the grid of the LOD 3 tiles
    middleGenerated = 0;
    loadAll(middle, {65, kRow});
    std::printf("crossing the edge: %d tiles generated (%zu kept), %d for the same walk in the "
                "middle, %zu selected\n",
                acrossTheEdge, kept, middleGenerated, manager.selection().size());
    TEST_CHECK(acrossTheEdge == middleGenerated && acrossTheEdge > 0);

    const TileKey key = manager.selection().back().key;
    const TileKey image{{key.origin.x - kCount.x, key.origin.z + 2 * kCount.z}, key.lod};
    TEST_CHECK(manager.find(image) == manager.find(key) && manager.find(key) != nullptr);
    const ChunkCoord chunks[] = {{kEast - 1 + 3 * kCount.x, kRow}};
    manager.invalidate(chunks);
    TEST_CHECK(manager.pendingCount() > 0);
}

// The tile moved by whole worlds: its positions read in the frame of another image.
TileData moved(TileData data, int worldsX, int worldsZ) {
    data.originX += worldsX * kWorld.x;
    data.originZ += worldsZ * kWorld.z;
    return data;
}

std::vector<std::pair<double, double>> sortedFace(const TileData& data, seam_test::Plane plane) {
    std::vector<std::pair<double, double>> points;
    for (const seam_test::FacePoint p : seam_test::facePoints(data, plane)) {
        points.emplace_back(p.u, p.y);
    }
    std::sort(points.begin(), points.end());
    return points;
}

// Tiles on both sides of the edge x = world.x / 2 and of the edge z = world.z / 2 share their
// faces; a LOD 0 tile stitched to the LOD 1 tile across the edge lies on its outline.
void testFacesAcrossTheEdges(const TerrainTiles& tiles) {
    const seam_test::Plane eastEdge{true, kWorld.x / 2.0};
    const TileData east = tiles.generate({{{kEast, kRow}, 0}, 0});
    const TileData west = moved(tiles.generate({{{kWest, kRow}, 0}, 0}), 1, 0);
    const auto shared = sortedFace(east, eastEdge);
    TEST_CHECK(!shared.empty() && shared == sortedFace(west, eastEdge));

    const seam_test::Plane northEdge{false, kWorld.z / 2.0};
    const TileData north = tiles.generate({{{100, kNorth}, 0}, 0});
    const TileData south = moved(tiles.generate({{{100, kSouth}, 0}, 0}), 0, 1);
    const auto polar = sortedFace(north, northEdge);
    TEST_CHECK(!polar.empty() && polar == sortedFace(south, northEdge));

    const TileData coarse = moved(tiles.generate({{{kWest, kRow - 1}, 1}, 0}), 1, 0);
    const TileData stitched = tiles.generate({{{kEast, kRow}, 0}, terrain::kSeamPosX});
    const double first = kRow * terrain::kChunkSizeMeters;
    std::size_t finePoints = 0;
    std::size_t coarsePoints = 0;
    const double fineGap = seam_test::largestGap(stitched, coarse, eastEdge, first,
                                                 first + terrain::kChunkSizeMeters, finePoints);
    const double coarseGap = seam_test::largestGap(coarse, stitched, eastEdge, first,
                                                   first + terrain::kChunkSizeMeters, coarsePoints);
    std::printf("faces across the edges: %zu shared points (x edge), %zu (z edge); stitched to "
                "LOD 1: gaps %.2g and %.2g over %zu fine points\n",
                shared.size(), polar.size(), fineGap, coarseGap, finePoints);
    TEST_CHECK(finePoints > coarsePoints && coarsePoints >= 2);
    TEST_CHECK(fineGap <= 1e-5 && coarseGap <= 1e-5);
}

bool canonicalStore(const terrain::ModificationStore& store) {
    return std::all_of(store.byChunk().begin(), store.byChunk().end(), [](const auto& entry) {
        const ChunkCoord chunk = entry.first;
        return terrain::wrapChunk(chunk, kWorld) == chunk &&
               std::all_of(entry.second.sections.begin(), entry.second.sections.end(),
                           [](const auto& section) { return !section.second.empty(); });
    });
}

// A block removed on the edge x = world.x / 2 (a lattice column of both tiles there), given at
// that image, is stored at x = -world.x / 2 in the canonical range, invalidates the chunks on
// both sides, digs both tiles, which still share their face, and changes the density of the
// lattice point at both of its images.
void testBlockAcrossTheEdge(TerrainTiles& tiles) {
    const int z = kRow * 16 + 8;
    const int x = kCount.x * 8; // world.x / 2, the edge
    const int y = static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z))) - 1;
    const TileData eastBefore = tiles.generate({{{kEast, kRow}, 0}, 0});
    const TileData westBefore = tiles.generate({{{kWest, kRow}, 0}, 0});
    const float before = tiles.densityAt(x, y, z);
    const terrain::BlockCoord block{x, y, z};
    const std::vector<ChunkCoord> chunks = tiles.setBlock(block, terrain::BlockEdit::Removed);
    const auto has = [&chunks](ChunkCoord c) {
        return std::find(chunks.begin(), chunks.end(), c) != chunks.end();
    };
    TEST_CHECK(has({kEast, kRow}) && has({kWest, kRow}));
    TEST_CHECK(std::all_of(chunks.begin(), chunks.end(),
                           [](ChunkCoord c) { return terrain::wrapChunk(c, kWorld) == c; }));
    TEST_CHECK(tiles.blockAt({x, y, z}) == terrain::BlockEdit::Removed);
    TEST_CHECK(tiles.blockAt({x - kCount.x * 16, y, z}) == terrain::BlockEdit::Removed);
    TEST_CHECK(canonicalStore(tiles.modifications()));
    TEST_CHECK(tiles.modifications().blockAt({x - kCount.x * 16, y, z}) ==
               terrain::BlockEdit::Removed);
    const float edge = tiles.densityAt(x, y, z);
    TEST_CHECK(edge == tiles.densityAt(x - kCount.x * 16, y, z) && edge > before);

    const TileData east = tiles.generate({{{kEast, kRow}, 0}, 0});
    const TileData west = tiles.generate({{{kWest, kRow}, 0}, 0});
    const seam_test::Plane plane{true, kWorld.x / 2.0};
    TEST_CHECK(!(east.mesh == eastBefore.mesh) && !(west.mesh == westBefore.mesh));
    TEST_CHECK(sortedFace(east, plane) == sortedFace(moved(west, 1, 0), plane));
    tiles.clearBlock({x + kCount.x * 16, y, z});
    TEST_CHECK(tiles.modifications().empty());
}

// Plants removed and added at another image are stored in the canonical chunk and change the
// canonical tile.
void testPlantsAcrossTheEdge(TerrainTiles& tiles) {
    const TileKey key{{kWest, kRow}, 0};
    const TileData before = tiles.generate({key, 0});
    TEST_CHECK(before.plants.size() >= 3);
    if (before.plants.size() < 3) {
        return;
    }
    terrain::Plant removed = before.plants.front();
    removed.x += kWorld.x;
    terrain::Plant added = before.plants.back();
    added.x = before.originX + 8.25 + 2.0 * kWorld.x;
    added.z = before.originZ + 8.5 - kWorld.z;
    const std::vector<ChunkCoord> a = tiles.removePlant(removed);
    const std::vector<ChunkCoord> b = tiles.addPlant(added);
    TEST_CHECK(a == std::vector<ChunkCoord>{key.origin} &&
               b == std::vector<ChunkCoord>{key.origin});
    TEST_CHECK(canonicalStore(tiles.modifications()));
    const TileData after = tiles.generate({{{kWest + kCount.x, kRow}, 0}, 0});
    TEST_CHECK(after.request.key == key);
    TEST_CHECK(after.plants.size() == before.plants.size());
    TEST_CHECK(after.plants.back().x == before.originX + 8.25 &&
               after.plants.back().z == before.originZ + 8.5);
    TEST_CHECK(std::none_of(after.plants.begin(), after.plants.end(),
                            [&](const terrain::Plant& p) { return p == before.plants.front(); }));
    tiles.replaceModifications({});
}

} // namespace

int main() {
    testSelectionAcrossTheEdges();
    testCrossingTheEdge();
    TerrainTiles tiles(streaming_test::kSeed, test_relief::legacyRelief());
    testFacesAcrossTheEdges(tiles);
    testBlockAcrossTheEdge(tiles);
    testPlantsAcrossTheEdge(tiles);
    return test::result();
}
