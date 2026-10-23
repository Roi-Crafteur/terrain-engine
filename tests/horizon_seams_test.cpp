// Seams of the far horizon. Level 0 meets the outer LOD 3 tiles of a real selection: the
// borders of their meshes lie on the polyline of the level 0 samples (no step, no crack). A
// tile whose border is cut by a cave entrance leaves that polyline: the gap is measured and
// reported (Unreal hides such gaps with a skirt under the border of level 0). The even samples
// of a level are the samples of the next coarser one, bit for bit: their material, biome and
// flags always, their heights with point samples (crestRatio 0; the crests lift the levels 1
// and up by their own cells). Across an edge and the corner of the wrapped world the samples
// are those of the unwrapped positions (point samples), without a jump (with crests too).
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::HorizonCell;
using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;
using terrain::HorizonSampler;
using terrain::TileData;

constexpr double kTileTolerance = 1e-3; // meters: the rounding of Marching Cubes in floats

struct ChunkHash {
    std::size_t operator()(ChunkCoord c) const {
        return std::hash<std::int64_t>()((static_cast<std::int64_t>(c.x) << 32) ^
                                         static_cast<std::uint32_t>(c.z));
    }
};

// A vertical face of a tile: x (or z) = fixed, meters, world coordinates.
struct Face {
    bool alongX = false; // the face is a plane of constant x
    double fixed = 0.0;
};

// Largest distance from the vertices of the mesh on the face to the polyline of the ground
// sampled every spacing meters along it (linear between two samples), and their count.
struct FaceGap {
    double gap = 0.0;
    int vertices = 0;
};

FaceGap faceGap(const TileData& data, const Face& face, const terrain::Heightfield& field,
                int spacing) {
    FaceGap result;
    for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, n);
        const double fixed = face.alongX ? p.x : p.z;
        if (fixed != face.fixed) {
            continue;
        }
        const double along = face.alongX ? p.z : p.x;
        const double first = std::floor(along / spacing) * spacing;
        const double t = (along - first) / spacing;
        const auto ground = [&](double a) {
            return static_cast<double>(face.alongX ? field.heightAt(face.fixed, a)
                                                   : field.heightAt(a, face.fixed));
        };
        const double expected = ground(first) * (1.0 - t) + ground(first + spacing) * t;
        result.gap = std::max(result.gap, std::abs(p.y - expected));
        ++result.vertices;
    }
    return result;
}

// Outer faces of the LOD 3 tiles of a selection: faces with no selected tile beyond them.
std::vector<std::pair<terrain::TileRequest, Face>>
outerFaces(const std::vector<terrain::TileRequest>& requests) {
    std::unordered_set<ChunkCoord, ChunkHash> covered;
    for (const terrain::TileRequest& request : requests) {
        const int side = terrain::tileChunks(request.key.lod);
        for (int dz = 0; dz < side; ++dz) {
            for (int dx = 0; dx < side; ++dx) {
                covered.insert({request.key.origin.x + dx, request.key.origin.z + dz});
            }
        }
    }
    std::vector<std::pair<terrain::TileRequest, Face>> faces;
    for (const terrain::TileRequest& request : requests) {
        if (request.key.lod != terrain::kMaxLod) {
            continue;
        }
        const ChunkCoord o = request.key.origin;
        const int side = terrain::tileChunks(terrain::kMaxLod);
        const double x0 = o.x * terrain::kChunkSizeMeters;
        const double z0 = o.z * terrain::kChunkSizeMeters;
        const double size = terrain::tileSizeMeters(terrain::kMaxLod);
        if (!covered.contains({o.x - 1, o.z})) {
            faces.push_back({request, {true, x0}});
        }
        if (!covered.contains({o.x + side, o.z})) {
            faces.push_back({request, {true, x0 + size}});
        }
        if (!covered.contains({o.x, o.z - 1})) {
            faces.push_back({request, {false, z0}});
        }
        if (!covered.contains({o.x, o.z + side})) {
            faces.push_back({request, {false, z0 + size}});
        }
    }
    return faces;
}

// Heights of level 0 along a face of the given length, from its lowest sample.
std::vector<float> levelZeroAlong(const HorizonSampler& sampler, const Face& face, double first,
                                  double length) {
    const int pitch = terrain::levelPitch(sampler.params(), 0);
    const auto fixed = static_cast<std::int64_t>(face.fixed / pitch);
    const auto a0 = static_cast<std::int64_t>(first / pitch);
    const auto a1 = a0 + static_cast<std::int64_t>(length / pitch) + 1;
    const HorizonRect rect =
        face.alongX ? HorizonRect{fixed, a0, fixed + 1, a1} : HorizonRect{a0, fixed, a1, fixed + 1};
    std::vector<float> heights;
    for (const HorizonCell& cell : sampler.sample(0, rect)) {
        heights.push_back(cell.height);
    }
    return heights;
}

void testOuterTiles() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed);
    const HorizonParams params;
    const terrain::LodParams lod;
    terrain::validate(params, lod); // the tiles end inside level 0
    const HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    const ChunkCoord player = streaming_test::mountainChunk(tiles.heightfield());
    const auto faces = outerFaces(terrain::tileRequests(terrain::selectTiles(player, lod)));
    const int perSide = (2 * lod.viewDistance + 8) / terrain::tileChunks(terrain::kMaxLod);
    TEST_CHECK(faces.size() >= static_cast<std::size_t>(4 * perSide));
    double worst = 0.0;
    int vertices = 0;
    for (const auto& [request, face] : faces) {
        TEST_CHECK(request.seams == 0U); // the outer tiles are never stitched
        const TileData data = tiles.generate(request);
        const FaceGap gap = faceGap(data, face, tiles.heightfield(), 8);
        worst = std::max(worst, gap.gap);
        vertices += gap.vertices;
        // The samples of level 0 on the face are the ground of the tile columns, bit for bit.
        const double first = face.alongX ? data.originZ : data.originX;
        const std::vector<float> heights =
            levelZeroAlong(sampler, face, first, terrain::tileSizeMeters(terrain::kMaxLod));
        for (std::size_t a = 0; a < heights.size(); ++a) {
            const double along = first + 8.0 * static_cast<double>(a);
            const float ground = face.alongX ? tiles.heightfield().heightAt(face.fixed, along)
                                             : tiles.heightfield().heightAt(along, face.fixed);
            TEST_CHECK(heights[a] == ground);
        }
    }
    std::printf("outer LOD 3 faces: %zu, %d border vertices, largest gap to level 0: %.2e m\n",
                faces.size(), vertices, worst);
    TEST_CHECK(vertices > 0);
    TEST_CHECK(worst <= kTileTolerance);
}

// A tile whose border crosses the tube of a cave entrance: its mesh leaves the ground there.
// Today only the tiles of LOD 0 to 2 carve entrances, and the outer tiles are LOD 3: the
// horizon never meets such a border yet, but cliffs, arches (phase 12) and player edits
// (phase 15) will bring them. The gap is reported, not asserted.
void testCaveEntranceGap() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed, {}, {}, terrain::CaveParams{});
    const auto entrances = tiles.caves()->entrancesNear(
        streaming_test::kCenterX - 8192.0, streaming_test::kCenterZ - 8192.0,
        streaming_test::kCenterX + 8192.0, streaming_test::kCenterZ + 8192.0, tiles.caveGround());
    const terrain::CaveEntrance* chosen = nullptr;
    Face face;
    for (const terrain::CaveEntrance& entrance : entrances) {
        const double line =
            std::round(entrance.topX / terrain::kChunkSizeMeters) * terrain::kChunkSizeMeters;
        if (std::abs(line - entrance.topX) < 0.5 * entrance.tubeRadius) {
            chosen = &entrance;
            face = {true, line};
            break;
        }
    }
    TEST_CHECK(chosen != nullptr);
    if (chosen == nullptr) {
        return;
    }
    // The LOD 0 tile east of the face, cut by the entrance on its west border.
    const ChunkCoord chunk = terrain::worldToChunk(face.fixed + 1.0, chosen->topZ);
    const TileData cut = tiles.generate({{chunk, 0}, 0});
    const FaceGap gap = faceGap(cut, face, tiles.heightfield(), 1);
    // The LOD 3 tile there carves nothing: it still meets level 0.
    const ChunkCoord far = terrain::tileContaining(chunk, terrain::kMaxLod).origin;
    const TileData outer = tiles.generate({{far, terrain::kMaxLod}, 0});
    const double x0 = far.x * terrain::kChunkSizeMeters;
    const FaceGap outerGap = faceGap(outer, {true, x0}, tiles.heightfield(), 8);
    std::printf("cave entrance at (%.1f, %.1f), tube %.1f m: the LOD 0 tile border leaves the "
                "ground by %.1f m (%d vertices) - a skirt under level 0 would hide it; the LOD 3 "
                "tile there: %.2e m\n",
                chosen->topX, chosen->topZ, chosen->tubeRadius, gap.gap, gap.vertices,
                outerGap.gap);
    TEST_CHECK(gap.gap > 1.0); // the case is found and measured
    TEST_CHECK(outerGap.gap <= kTileTolerance);
}

// Sample J of level n + 1 is sample 2J of level n: the same material, biome and flags, and the
// same ground for point samples.
void testNestedLevels(int crestRatio) {
    const terrain::Heightfield field(streaming_test::kSeed);
    const terrain::Ecology ecology(streaming_test::kSeed);
    HorizonParams params;
    params.crestRatio = crestRatio;
    const HorizonSampler sampler(field, ecology, params);
    const terrain::WorldSize world = params.world;
    for (const auto& [x, z] :
         {std::pair{-1'441'792.0, -3'932'160.0}, std::pair{world.x / 2.0, world.z / 2.0}}) {
        for (int level = 0; level + 1 < params.levels; ++level) {
            const HorizonIndex c = terrain::canonicalCenter(params, level + 1, x, z);
            const HorizonRect coarse{c.x - 6, c.z - 6, c.x + 6, c.z + 6};
            const HorizonRect fine{2 * coarse.x0, 2 * coarse.z0, 2 * coarse.x1 - 1,
                                   2 * coarse.z1 - 1};
            const std::vector<HorizonCell> a = sampler.sample(level + 1, coarse);
            const std::vector<HorizonCell> b = sampler.sample(level, fine);
            for (std::int64_t k = 0; k < coarse.height(); ++k) {
                for (std::int64_t i = 0; i < coarse.width(); ++i) {
                    const HorizonCell& p = a[static_cast<std::size_t>(k * coarse.width() + i)];
                    const HorizonCell& q =
                        b[static_cast<std::size_t>(2 * k * fine.width() + 2 * i)];
                    TEST_CHECK(p.material == q.material && p.biome == q.biome);
                    TEST_CHECK(p.flags == q.flags && (crestRatio != 0 || p.height == q.height));
                }
            }
        }
    }
}

// A rectangle across the east edge and one at the corner of the world: the samples of the
// unwrapped positions (point samples), and no larger jump across the edges than between other
// neighbors.
void testWorldEdges(int crestRatio) {
    const terrain::Heightfield field(streaming_test::kSeed);
    const terrain::Ecology ecology(streaming_test::kSeed);
    HorizonParams params;
    params.crestRatio = crestRatio;
    const HorizonSampler sampler(field, ecology, params);
    for (const int level : {0, 5, 9}) {
        const int pitch = terrain::levelPitch(params, level);
        const HorizonIndex counts = terrain::levelCounts(params, level);
        for (const std::int64_t row : {std::int64_t{0}, counts.z / 2}) {
            const HorizonRect rect{counts.x / 2 - 20, row - 20, counts.x / 2 + 20, row + 20};
            const std::vector<HorizonCell> cells = sampler.sample(level, rect);
            double seamJump = 0.0;
            double otherJump = 0.0;
            std::size_t n = 0;
            for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
                for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
                    const double x = terrain::samplePosition(i, pitch); // beyond the edges
                    const double z = terrain::samplePosition(k, pitch);
                    TEST_CHECK(crestRatio != 0 || cells[n].height == field.heightAt(x, z));
                    if (i + 1 == rect.x1) {
                        continue;
                    }
                    const double jump = std::abs(cells[n + 1].height - cells[n].height);
                    double& largest = 2 * (i + 1) == counts.x ? seamJump : otherJump;
                    largest = std::max(largest, jump);
                }
            }
            TEST_CHECK(seamJump <= 2.0 * otherJump);
        }
    }
}

} // namespace

int main() {
    testOuterTiles();
    testCaveEntranceGap();
    for (const int crestRatio : {0, HorizonParams{}.crestRatio}) {
        testNestedLevels(crestRatio);
        testWorldEdges(crestRatio);
    }
    return test::result();
}
