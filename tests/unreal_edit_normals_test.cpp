// The normals of a tile after the player broke a sphere of 4 m into the ground (agent 28): every
// vertex of the crater has a normal toward the air of the crater (its center), the winding of its
// triangles agrees with the normals (the side Unreal draws), and the Unreal mesh keeps both. A
// crater found black in Unreal comes from its light, not from its mesh, when this test passes.
// Also measures the facets of the crater: the longest edge of its triangles.
#include "block_targeting.hpp"
#include "terrain_session.hpp"
#include "test_assert.hpp"
#include "tile_mesh_conversion.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {

using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::Vec3f;

Vec3f sub(Vec3f a, Vec3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f cross(Vec3f a, Vec3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(Vec3f a) {
    return std::sqrt(dot(a, a));
}

struct CraterReport {
    int vertices = 0;      // vertices of the crater (within the sphere, under the old ground)
    int inward = 0;        // their normals toward the center of the sphere (the air)
    int triangles = 0;     // triangles of the crater
    int agreeing = 0;      // their winding on the side of their vertex normals
    float longestEdge = 0; // meters
};

CraterReport inspect(const terrain::TileData& data, terrain::unreal::CorePoint center, double radius,
                     double ground) {
    CraterReport report;
    const terrain::Mesh& mesh = data.mesh;
    const Vec3f c{static_cast<float>(center.x - data.originX),
                  static_cast<float>(center.y - data.originY),
                  static_cast<float>(center.z - data.originZ)};
    const float under = static_cast<float>(ground - data.originY - 0.2);
    const auto inCrater = [&](const Vec3f& v) {
        return length(sub(v, c)) < radius + 0.6 && v.y < under;
    };
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (inCrater(mesh.vertices[i])) {
            ++report.vertices;
            report.inward += dot(mesh.normals[i], sub(c, mesh.vertices[i])) > 0.0f ? 1 : 0;
        }
    }
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], d = mesh.indices[t + 2];
        if (!inCrater(mesh.vertices[a]) || !inCrater(mesh.vertices[b]) ||
            !inCrater(mesh.vertices[d])) {
            continue;
        }
        ++report.triangles;
        // The core: counter-clockwise triangles, (b - a) x (c - a) toward the air.
        const Vec3f face = cross(sub(mesh.vertices[b], mesh.vertices[a]),
                                 sub(mesh.vertices[d], mesh.vertices[a]));
        const Vec3f normal{mesh.normals[a].x + mesh.normals[b].x + mesh.normals[d].x,
                           mesh.normals[a].y + mesh.normals[b].y + mesh.normals[d].y,
                           mesh.normals[a].z + mesh.normals[b].z + mesh.normals[d].z};
        report.agreeing += dot(face, normal) > 0.0f ? 1 : 0;
        report.longestEdge = std::max({report.longestEdge,
                                       length(sub(mesh.vertices[b], mesh.vertices[a])),
                                       length(sub(mesh.vertices[d], mesh.vertices[b])),
                                       length(sub(mesh.vertices[a], mesh.vertices[d]))});
    }
    return report;
}

void testCrater(double x, double z) {
    terrain::unreal::SessionParams params;
    params.stream.lod = {{2, 4, 8}, 6};
    params.stream.workerThreads = 2;
    terrain::unreal::TerrainSession session(params);
    const double ground = session.tiles().heightfield().heightAt(x, z);
    const terrain::unreal::BuildHit hit{{x, ground, z}, {0.0, 1.0, 0.0}, 3.0};
    const auto target = buildTarget(hit, terrain::unreal::BuildTool::Sphere4,
                                    terrain::unreal::BuildAction::Break);
    session.applyEdit(target.edit);
    const terrain::unreal::UnrealFrame frame = session.frameAt(x, z);
    const terrain::unreal::UnrealPoint player = terrain::unreal::toUnreal(frame, {x, 0.0, z});
    TEST_CHECK(session.streamer().warmUp(player, frame, 1, std::chrono::seconds(60)).complete);
    const ChunkCoord chunk{static_cast<int>(std::floor(x / 16.0)),
                           static_cast<int>(std::floor(z / 16.0))};
    const terrain::TileData* data = session.streamer().manager().find(TileKey{chunk, 0});
    TEST_CHECK(data != nullptr);
    if (data == nullptr) {
        return;
    }
    const terrain::unreal::CorePoint center{static_cast<double>(target.edit.anchor.x),
                                    static_cast<double>(target.edit.anchor.y),
                                    static_cast<double>(target.edit.anchor.z)};
    const CraterReport report = inspect(*data, center, 2.0, ground);
    std::printf("crater at (%.0f, %.0f): %d vertices, %d with a normal toward its air; %d "
                "triangles, %d wound on the side of their normals; longest edge %.2f m\n",
                x, z, report.vertices, report.inward, report.triangles, report.agreeing,
                report.longestEdge);
    TEST_CHECK(report.vertices > 10);
    TEST_CHECK(report.inward == report.vertices);
    TEST_CHECK(report.triangles > 10 && report.agreeing == report.triangles);
    // The Unreal mesh keeps the normals (the axes swapped) and the order of the indices.
    const terrain::unreal::UnrealMesh unreal = terrain::unreal::convertMesh(data->mesh, 0.0);
    TEST_CHECK(unreal.normals.size() == data->mesh.normals.size());
    TEST_CHECK(unreal.triangles.size() == data->mesh.indices.size());
}

// The vertices of after that are not in before, and the extent of their box (meters).
struct Change {
    int vertices = 0;
    float extent = 0.0f;
    float longestEdge = 0.0f;
};

Change compare(const terrain::Mesh& before, const terrain::Mesh& after) {
    const auto known = [&before](const Vec3f& v) {
        return std::any_of(before.vertices.begin(), before.vertices.end(), [&v](const Vec3f& w) {
            return w.x == v.x && w.y == v.y && w.z == v.z;
        });
    };
    Change change;
    std::vector<bool> fresh(after.vertices.size(), false);
    Vec3f low{1e9f, 1e9f, 1e9f};
    Vec3f high{-1e9f, -1e9f, -1e9f};
    for (std::size_t i = 0; i < after.vertices.size(); ++i) {
        if (known(after.vertices[i])) {
            continue;
        }
        fresh[i] = true;
        ++change.vertices;
        const Vec3f& v = after.vertices[i];
        low = {std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z)};
        high = {std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z)};
    }
    if (change.vertices > 0) {
        change.extent = std::max({high.x - low.x, high.y - low.y, high.z - low.z});
    }
    for (std::size_t t = 0; t + 2 < after.indices.size(); t += 3) {
        const std::uint32_t a = after.indices[t], b = after.indices[t + 1], c = after.indices[t + 2];
        if (fresh[a] || fresh[b] || fresh[c]) {
            change.longestEdge = std::max({change.longestEdge,
                                           length(sub(after.vertices[b], after.vertices[a])),
                                           length(sub(after.vertices[c], after.vertices[b])),
                                           length(sub(after.vertices[a], after.vertices[c]))});
        }
    }
    return change;
}

// A block of 1 m placed on the ground, seen by the tiles of LOD 0 to 3 over it: from LOD 1 on,
// the lattice samples the edited density every 2, 4 or 8 m, so a block of 1 m changes a whole
// cell when it reaches one of its lattice points (printed for the report; facets and spikes).
void measureLods(double x, double z) {
    terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const double ground = tiles.heightfield().heightAt(x, z);
    const terrain::unreal::BuildHit hit{{x, ground, z}, {0.0, 1.0, 0.0}, 3.0};
    const auto block = buildTarget(hit, terrain::unreal::BuildTool::Block,
                                   terrain::unreal::BuildAction::Place);
    const ChunkCoord chunk{static_cast<int>(std::floor(x / 16.0)),
                           static_cast<int>(std::floor(z / 16.0))};
    std::vector<terrain::TileData> before;
    for (int lod = 0; lod < 4; ++lod) {
        before.push_back(tiles.generate({terrain::tileContaining(chunk, lod), 0}));
    }
    (void)tiles.applyEdit(block.edit);
    for (int lod = 0; lod < 4; ++lod) {
        const terrain::TileData after = tiles.generate({terrain::tileContaining(chunk, lod), 0});
        const Change change = compare(before[static_cast<std::size_t>(lod)].mesh, after.mesh);
        std::printf("block placed at (%d, %d, %d), LOD %d (cell %.0f m): %d new vertices over "
                    "%.2f m, longest new edge %.2f m\n",
                    block.edit.anchor.x, block.edit.anchor.y, block.edit.anchor.z, lod,
                    terrain::tileCellMeters(lod), change.vertices, change.extent,
                    change.longestEdge);
        if (lod == 0) {
            TEST_CHECK(change.vertices > 0 && change.longestEdge < 2.0f);
        }
    }
}

} // namespace

int main() {
    testCrater(8.3, 8.6);      // the plain of the origin
    testCrater(-120.4, 37.2);  // another place, on a slope
    for (const double x : {8.3, 21.3, 31.7, 64.0}) {
        measureLods(x, 16.0); // on and off the lattice points of the coarse LODs
    }
    return test::result();
}
