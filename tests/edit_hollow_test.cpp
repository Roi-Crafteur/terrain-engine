// The hollows of the sphere tools (agent 41). A sphere centered on a block keeps the blocks within
// its radius (contains): the tools of agent 28 (radii 1 m and 2 m) kept single blocks at the ends
// of their axes, which Marching Cubes draws as pyramids of 0.5 m, the spikes seen in Unreal. At
// the radii of the tools now (26 / 16 and 39 / 16 m), no block of the sphere but its center has
// fewer than two neighbors by a face in it, and the hollow broken into flat ground, into a slope
// and into the side of a mass of placed blocks is round: no vertex more than kOut outside the
// sphere asked, none more than kIn inside it. The normals of the hollow turn smoothly over the
// steps of the blocks (tile_edits.hpp: lightEdits): each one within kMaxRadial of the direction
// of the center of the sphere, as on a round hollow, and every vertex away from the edits keeps
// the bits of its normal. (The angle between the normals of two vertices of a triangle, printed,
// stays about 45 degrees: the curvature of a hollow of 2 m over vertices 1 m apart.)
#include "block_targeting.hpp"
#include "test_assert.hpp"

#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::TileData;
using terrain::Vec3f;
using terrain::VolumeEdit;
using terrain::unreal::BuildAction;
using terrain::unreal::BuildTool;

constexpr float kOut = 0.3f;       // meters beyond the sphere asked
constexpr float kIn = 0.6f;        // meters inside it
// Degrees between the normal of a vertex of the hollow and the direction of the center of the
// sphere: measured without the light of the edits (tile_edits.hpp) at 17 to 34 at most, 7.6 to 16
// on average; with it, 5 to 13.8 at most, 3.4 to 8.9 on average.
constexpr float kMaxRadial = 15.0f;
constexpr float kMeanRadial = 10.0f;
constexpr float kRimDepth = 0.3f;  // the vertices of the rim, near the old ground, left out

float dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f sub(Vec3f a, Vec3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float length(Vec3f a) {
    return std::sqrt(dot(a, a));
}

// The blocks of a sphere, but its center, with fewer than two neighbors by a face in it.
int loneBlocks(const VolumeEdit& edit) {
    const terrain::BlockBox box = terrain::editBounds(edit);
    int lone = 0;
    for (int y = box.min.y; y <= box.max.y; ++y) {
        for (int z = box.min.z; z <= box.max.z; ++z) {
            for (int x = box.min.x; x <= box.max.x; ++x) {
                const BlockCoord b{x, y, z};
                if (!terrain::contains(edit, b) || b == edit.anchor) {
                    continue;
                }
                int neighbors = 0;
                for (const std::array<int, 3> d : {std::array<int, 3>{1, 0, 0}, {-1, 0, 0},
                                                   {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}) {
                    neighbors += terrain::contains(edit, {x + d[0], y + d[1], z + d[2]}) ? 1 : 0;
                }
                lone += neighbors < 2 ? 1 : 0;
            }
        }
    }
    return lone;
}

struct Hollow {
    int vertices = 0;
    float out = 0.0f;  // the farthest vertex beyond the radius
    float in = 0.0f;   // the nearest vertex inside it
    float turn = 0.0f; // the largest angle between two normals of a triangle, degrees
    float radialMean = 0.0f; // the angle of the normals with the direction of the center, degrees
    float radialMax = 0.0f;
};

float degrees(Vec3f a, Vec3f b) {
    const float c = std::clamp(dot(a, b) / (length(a) * length(b)), -1.0f, 1.0f);
    return std::acos(c) * 57.29578f;
}

// The vertices of the hollow: within radius + 1 m of the center, deeper than kRimDepth under the
// old ground (inside(v) true).
template <typename Inside>
Hollow measure(const TileData& data, Vec3f center, float radius, Inside inside) {
    Hollow h;
    const terrain::Mesh& mesh = data.mesh;
    std::vector<bool> in(mesh.vertices.size(), false);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f& v = mesh.vertices[i];
        const float d = length(sub(v, center));
        if (d > radius + 1.0f || !inside(v)) {
            continue;
        }
        in[i] = true;
        ++h.vertices;
        h.out = std::max(h.out, d - radius);
        h.in = std::max(h.in, radius - d);
        const float radial = degrees(mesh.normals[i], sub(center, v));
        h.radialMean += radial;
        h.radialMax = std::max(h.radialMax, radial);
    }
    h.radialMean /= static_cast<float>(std::max(h.vertices, 1));
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::array<std::uint32_t, 3> v{mesh.indices[t], mesh.indices[t + 1],
                                             mesh.indices[t + 2]};
        if (!in[v[0]] || !in[v[1]] || !in[v[2]]) {
            continue;
        }
        for (std::size_t e = 0; e < 3; ++e) {
            h.turn = std::max(h.turn, degrees(mesh.normals[v[e]], mesh.normals[v[(e + 1) % 3]]));
        }
    }
    return h;
}

// Every vertex of after farther than reach from the center, at the position of a vertex of
// before, has its normal bit for bit.
bool keepsFarNormals(const TileData& before, const TileData& after, Vec3f center, float reach) {
    for (std::size_t i = 0; i < after.mesh.vertices.size(); ++i) {
        const Vec3f& v = after.mesh.vertices[i];
        if (length(sub(v, center)) < reach) {
            continue;
        }
        for (std::size_t j = 0; j < before.mesh.vertices.size(); ++j) {
            const Vec3f& w = before.mesh.vertices[j];
            if (v.x == w.x && v.y == w.y && v.z == w.z &&
                std::memcmp(&after.mesh.normals[i], &before.mesh.normals[j], sizeof(Vec3f)) != 0) {
                return false;
            }
        }
    }
    return true;
}

ChunkCoord chunkOf(double x, double z) {
    return {static_cast<int>(std::floor(x / 16.0)), static_cast<int>(std::floor(z / 16.0))};
}

// A sphere of radius sixteenths broken at (x, z) into the ground, the hollow of the LOD 0 tile.
Hollow breakGround(double x, double z, std::uint16_t sixteenths, bool expectRound) {
    terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const double ground = tiles.heightfield().heightAt(x, z);
    terrain::unreal::BuildHit hit{{x, ground, z}, {0.0, 1.0, 0.0}, 3.0};
    VolumeEdit edit = terrain::unreal::buildTarget(hit, BuildTool::Sphere4, BuildAction::Break).edit;
    edit.radius = sixteenths;
    const terrain::TileKey key{chunkOf(x, z), 0};
    const TileData before = tiles.generate({key, 0});
    (void)tiles.applyEdit(edit);
    const TileData after = tiles.generate({key, 0});
    const Vec3f center{static_cast<float>(edit.anchor.x - after.originX),
                       static_cast<float>(edit.anchor.y - after.originY),
                       static_cast<float>(edit.anchor.z - after.originZ)};
    const float radius = static_cast<float>(sixteenths) / 16.0f;
    const terrain::Heightfield& field = tiles.heightfield();
    const Hollow h = measure(after, center, radius, [&](const Vec3f& v) {
        const double old = field.heightAt(after.originX + v.x, after.originZ + v.z);
        return after.originY + v.y < old - kRimDepth;
    });
    std::printf("radius %.4f m at (%.1f, %.1f), ground %.2f m: %d vertices, %.2f m out, %.2f m "
                "in, normals turn %.1f degrees, %.1f from the center (%.1f at most); %d lone blocks\n",
                radius, x, z, ground, h.vertices, h.out, h.in, h.turn, h.radialMean, h.radialMax,
                loneBlocks(edit));
    TEST_CHECK(h.vertices > 10);
    if (expectRound) {
        TEST_CHECK(h.out <= kOut && h.in <= kIn);
        TEST_CHECK(h.radialMax <= kMaxRadial && h.radialMean <= kMeanRadial);
        TEST_CHECK(keepsFarNormals(before, after, center, radius + 2.0f));
    }
    return h;
}

// The large sphere broken into the side (+x) of a mass of 8 x 8 x 8 placed blocks.
void breakWall() {
    terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const int x0 = 2;
    const int z0 = 4;
    const int ground = static_cast<int>(std::floor(tiles.heightfield().heightAt(x0 + 4, z0 + 4)));
    VolumeEdit mass;
    mass.shape = terrain::EditShape::Box;
    mass.mode = terrain::EditMode::Add;
    mass.anchor = {x0, ground, z0};
    mass.size = {8, 8, 8};
    (void)tiles.applyEdit(mass);
    const double face = x0 + 7.5; // the side of the mass
    terrain::unreal::BuildHit hit{{face, ground + 4.0, z0 + 4.0}, {1.0, 0.0, 0.0}, 3.0};
    const VolumeEdit edit =
        terrain::unreal::buildTarget(hit, BuildTool::Sphere4, BuildAction::Break).edit;
    (void)tiles.applyEdit(edit);
    const TileData after = tiles.generate({{chunkOf(x0, z0), 0}, 0});
    const Vec3f center{static_cast<float>(edit.anchor.x - after.originX),
                       static_cast<float>(edit.anchor.y - after.originY),
                       static_cast<float>(edit.anchor.z - after.originZ)};
    const float radius = static_cast<float>(edit.radius) / 16.0f;
    const float side = static_cast<float>(face - 0.5 - after.originX);
    const Hollow h = measure(after, center, radius, [&](const Vec3f& v) { return v.x < side; });
    std::printf("large sphere in the side of a mass: %d vertices, %.2f m out, %.2f m in, normals "
                "turn %.1f degrees, %.1f from the center (%.1f at most)\n",
                h.vertices, h.out, h.in, h.turn, h.radialMean, h.radialMax);
    TEST_CHECK(h.vertices > 10);
    TEST_CHECK(h.out <= kOut && h.in <= kIn);
    TEST_CHECK(h.radialMax <= kMaxRadial && h.radialMean <= kMeanRadial);
}

void testTools() {
    for (const BuildTool tool : {BuildTool::Sphere2, BuildTool::Sphere4}) {
        terrain::unreal::BuildHit hit{{0.3, 0.2, 0.1}, {0.0, 1.0, 0.0}, 3.0};
        const VolumeEdit edit = terrain::unreal::buildTarget(hit, tool, BuildAction::Break).edit;
        TEST_CHECK(loneBlocks(edit) == 0);
    }
    // The radii of agent 28: the cross of 7 blocks, the cube of 27 with 6 blocks on its axes.
    VolumeEdit old;
    old.shape = terrain::EditShape::Sphere;
    old.radius = 16;
    TEST_CHECK(loneBlocks(old) == 6);
    old.radius = 32;
    TEST_CHECK(loneBlocks(old) == 6);
}

} // namespace

int main() {
    testTools();
    for (const std::uint16_t r : {std::uint16_t{26}, std::uint16_t{39}}) {
        (void)breakGround(8.3, 8.6, r, true);     // the plain of the origin
        (void)breakGround(-120.4, 37.2, r, true); // a slope
    }
    // The large sphere of agent 28 (2 m): its single blocks stick out of the sphere.
    const Hollow old = breakGround(8.3, 8.6, 32, false);
    TEST_CHECK(old.out > kOut);
    breakWall();
    return test::result();
}
