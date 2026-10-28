// The seam between the tiles and the far horizon under player edits. Level 0 meets the outer LOD 3
// tiles of a real selection on their columns: a crater of 10 m, a razed square and a heap of
// earth across their outer faces leave no step there (the level 0 sample is the top of the tile
// column within the floats of Marching Cubes), and no crack wider than a cell of the lattice
// between the columns, where the walls of the edits cut the faces; on a razed floor and away
// from the edits the borders lie on the polyline of level 0 as without edits.
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::TerrainTiles;
using terrain::TileRequest;
using terrain::VolumeEdit;

constexpr double kTileTolerance = 1e-3; // meters: the floats of Marching Cubes and of the levels
constexpr double kCell = 8.0;           // the lattice of the LOD 3 tiles and of level 0

// A vertical face of a tile: x (or z) = fixed, from first along the other axis.
struct Face {
    TileRequest request;
    bool alongX = false; // a plane of constant x
    double fixed = 0.0;
    double first = 0.0;
};

// The outer faces of the LOD 3 tiles of a selection: no selected tile beyond them.
std::vector<Face> outerFaces(const std::vector<TileRequest>& requests) {
    std::set<std::pair<int, int>> covered;
    for (const TileRequest& request : requests) {
        const int side = terrain::tileChunks(request.key.lod);
        for (int dz = 0; dz < side; ++dz) {
            for (int dx = 0; dx < side; ++dx) {
                covered.insert({request.key.origin.x + dx, request.key.origin.z + dz});
            }
        }
    }
    std::vector<Face> faces;
    const int side = terrain::tileChunks(terrain::kMaxLod);
    const double size = terrain::tileSizeMeters(terrain::kMaxLod);
    for (const TileRequest& request : requests) {
        const ChunkCoord o = request.key.origin;
        const double x0 = o.x * terrain::kChunkSizeMeters;
        const double z0 = o.z * terrain::kChunkSizeMeters;
        if (request.key.lod != terrain::kMaxLod) {
            continue;
        }
        if (!covered.contains({o.x - 1, o.z})) {
            faces.push_back({request, true, x0, z0});
        }
        if (!covered.contains({o.x + side, o.z})) {
            faces.push_back({request, true, x0 + size, z0});
        }
        if (!covered.contains({o.x, o.z - 1})) {
            faces.push_back({request, false, z0, x0});
        }
        if (!covered.contains({o.x, o.z + side})) {
            faces.push_back({request, false, z0 + size, x0});
        }
    }
    return faces;
}

int groundAt(const TerrainTiles& tiles, double x, double z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// World position of a point of a face.
std::pair<double, double> worldOf(const Face& face, double along) {
    return face.alongX ? std::pair{face.fixed, along} : std::pair{along, face.fixed};
}

// The edits across three faces: a crater on a column, a razed square of 100 m and a heap.
std::vector<VolumeEdit> editsAcross(const TerrainTiles& tiles, const Face& crater,
                                    const Face& razed, const Face& heap) {
    const auto [cx, cz] = worldOf(crater, crater.first + 64.0);
    const auto [rx, rz] = worldOf(razed, razed.first + 64.0);
    const auto [hx, hz] = worldOf(heap, heap.first + 64.0);
    int low = groundAt(tiles, rx, rz);
    int high = low;
    for (double dz = -50.0; dz <= 50.0; dz += 4.0) {
        for (double dx = -50.0; dx <= 50.0; dx += 4.0) {
            low = std::min(low, groundAt(tiles, rx + dx, rz + dz));
            high = std::max(high, groundAt(tiles, rx + dx, rz + dz));
        }
    }
    const auto at = [](double v) { return static_cast<int>(v); };
    return {
        {EditShape::Sphere, EditMode::Remove, {at(cx), groundAt(tiles, cx, cz), at(cz)}, {}, 80},
        {EditShape::Box,
         EditMode::Remove,
         {at(rx) - 50, low - 20, at(rz) - 50},
         {100, static_cast<std::uint16_t>(high - low + 40), 100},
         0},
        {EditShape::Sphere,
         EditMode::Add,
         {at(hx), groundAt(tiles, hx, hz) - 5, at(hz)},
         {},
         30 * 16}};
}

// Level 0 along a face: its 17 samples, from its first column.
std::vector<float> levelZeroAlong(const terrain::HorizonSampler& sampler, const Face& face) {
    const auto fixed = static_cast<std::int64_t>(face.fixed / kCell);
    const auto a0 = static_cast<std::int64_t>(face.first / kCell);
    const terrain::HorizonRect rect = face.alongX
                                          ? terrain::HorizonRect{fixed, a0, fixed + 1, a0 + 17}
                                          : terrain::HorizonRect{a0, fixed, a0 + 17, fixed + 1};
    std::vector<float> heights;
    for (const terrain::HorizonCell& cell : sampler.sample(0, rect)) {
        heights.push_back(cell.height);
    }
    return heights;
}

// Distance from a point of the face to the polyline of level 0, in the plane of the face.
double distanceToPolyline(const std::vector<float>& heights, double first, double u, double y) {
    double best = 1e30;
    for (std::size_t a = 0; a + 1 < heights.size(); ++a) {
        const double u0 = first + kCell * static_cast<double>(a);
        const double du = kCell;
        const double dy = static_cast<double>(heights[a + 1]) - heights[a];
        const double t =
            std::clamp(((u - u0) * du + (y - heights[a]) * dy) / (du * du + dy * dy), 0.0, 1.0);
        best = std::min(best, std::hypot(u - (u0 + t * du), y - (heights[a] + t * dy)));
    }
    return best;
}

struct FaceSeam {
    double columnStep = 0.0; // largest difference between level 0 and the top of a column
    double crack = 0.0;      // largest distance of a border vertex to the polyline of level 0
    double flatCrack = 0.0;  // the same between two columns of one height
    int edited = 0;          // columns whose level 0 sample the edits changed
};

FaceSeam seamOf(const TerrainTiles& tiles, const terrain::HorizonSampler& sampler,
                const Face& face) {
    const terrain::TileData data = tiles.generate(face.request);
    const std::vector<float> heights = levelZeroAlong(sampler, face);
    FaceSeam seam;
    std::vector<std::optional<double>> tops(heights.size());
    for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, n);
        if ((face.alongX ? p.x : p.z) != face.fixed) {
            continue;
        }
        const double u = face.alongX ? p.z : p.x;
        const double cell = (u - face.first) / kCell;
        seam.crack = std::max(seam.crack, distanceToPolyline(heights, face.first, u, p.y));
        const auto a = static_cast<std::size_t>(std::floor(cell));
        if (cell == std::floor(cell)) {
            tops[a] = tops[a] ? std::max(*tops[a], p.y) : p.y;
        } else if (a + 1 < heights.size() && heights[a] == heights[a + 1]) {
            seam.flatCrack = std::max(seam.flatCrack, std::abs(p.y - heights[a]));
        }
    }
    for (std::size_t a = 0; a < heights.size(); ++a) {
        const auto [x, z] = worldOf(face, face.first + kCell * static_cast<double>(a));
        seam.columnStep =
            std::max(seam.columnStep, tops[a] ? std::abs(*tops[a] - heights[a]) : 1e9);
        seam.edited += heights[a] != tiles.heightfield().heightAt(x, z) ? 1 : 0;
    }
    return seam;
}

void testEditedFaces() {
    TerrainTiles tiles(streaming_test::kSeed);
    const terrain::HorizonParams params;
    const terrain::LodParams lod;
    terrain::validate(params, lod);
    const ChunkCoord player = streaming_test::mountainChunk(tiles.heightfield());
    const std::vector<Face> faces =
        outerFaces(terrain::tileRequests(terrain::selectTiles(player, lod)));
    // Three faces apart: the first along x, the first along z and the last along x.
    const auto alongX =
        std::find_if(faces.begin(), faces.end(), [](const Face& f) { return f.alongX; });
    const auto alongZ =
        std::find_if(faces.begin(), faces.end(), [](const Face& f) { return !f.alongX; });
    const auto lastX =
        std::find_if(faces.rbegin(), faces.rend(), [](const Face& f) { return f.alongX; });
    TEST_CHECK(alongX != faces.end() && alongZ != faces.end() && lastX != faces.rend());
    if (alongX == faces.end() || alongZ == faces.end() || lastX == faces.rend()) {
        return;
    }
    const std::vector<Face> edited = {*alongX, *alongZ, *lastX};
    terrain::HorizonEditMap map(tiles.heightfield());
    for (const VolumeEdit& edit : editsAcross(tiles, edited[0], edited[1], edited[2])) {
        map.stage(tiles, tiles.applyEdit(edit));
    }
    map.applyStaged();
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params,
                                          map.source());
    const char* names[] = {"crater of 10 m", "razed square of 100 m", "heap of 30 m"};
    for (std::size_t n = 0; n < edited.size(); ++n) {
        const FaceSeam seam = seamOf(tiles, sampler, edited[n]);
        std::printf("%s across an outer face: %d columns edited, step on the columns %.2e m, "
                    "largest crack %.2f m, %.2e m between columns of one height\n",
                    names[n], seam.edited, seam.columnStep, seam.crack, seam.flatCrack);
        TEST_CHECK(seam.edited > 0 && seam.columnStep <= kTileTolerance);
        TEST_CHECK(seam.crack <= kCell && seam.flatCrack <= kTileTolerance);
    }
    // The other faces: as without edits.
    double worst = 0.0;
    for (std::size_t f = 0; f < faces.size(); f += 7) {
        const FaceSeam seam = seamOf(tiles, sampler, faces[f]);
        worst = std::max({worst, seam.columnStep, seam.edited == 0 ? seam.crack : 0.0});
    }
    std::printf("other outer faces: largest gap %.2e m\n", worst);
    TEST_CHECK(worst <= kTileTolerance);
}

} // namespace

int main() {
    testEditedFaces();
    return test::result();
}
