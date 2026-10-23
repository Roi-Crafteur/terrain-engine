// Textures and grids of the GPU horizon (Bridge/horizon_textures): the texel of a sample (the
// ocean flat at the sea level with its ground under the sea, a mountain lake kept, the surface
// of the land packed), the regions of a rectangle split where the slots wrap, simulated textures
// written by the epochs along a walk across the edge of the world holding the texel of every
// sample of every window at its slot, and the grids: the body without its bounds vertex, the 9
// trims and their variants.
#include "horizon_textures.hpp"

#include "horizon_epochs.hpp"
#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonCell;
using terrain::HorizonRect;
using terrain::unreal::CellRect;
using terrain::unreal::GridMesh;
using terrain::unreal::GridVertexKind;
using terrain::unreal::HorizonTexel;
using terrain::unreal::kTexelFloats;

void testTexels() {
    HorizonCell land;
    land.height = 812.5f;
    land.gradX = 0.25f;
    land.gradZ = -0.5f;
    land.material = terrain::SurfaceMaterial::Pebbles;
    land.biome = terrain::Biome::Mountain;
    const HorizonTexel ground = terrain::unreal::horizonTexel(land, true);
    TEST_CHECK(ground.height == 812.5f && ground.gradX == 0.25f && ground.gradZ == -0.5f);
    // The surface of the land: its material and its biome, packed.
    TEST_CHECK((terrain::unreal::unpackSurface(ground.color) ==
                terrain::unreal::SurfaceCode{terrain::SurfaceMaterial::Pebbles,
                                             terrain::Biome::Mountain}));
    TEST_CHECK(!terrain::unreal::isSeaTexel(ground));
    TEST_CHECK(terrain::unreal::texelGround(ground) == 812.5f);
    // The ocean: flat at the sea level, its normal up, the ground under the sea in the last float.
    HorizonCell ocean;
    ocean.height = -42.0f;
    ocean.gradX = 0.1f;
    ocean.flags = terrain::kHorizonWater | terrain::kHorizonSea;
    const HorizonTexel sea = terrain::unreal::horizonTexel(ocean, true);
    TEST_CHECK((sea == HorizonTexel{0.0f, 0.0f, 0.0f, -42.0f}));
    TEST_CHECK(terrain::unreal::isSeaTexel(sea) && terrain::unreal::texelGround(sea) == -42.0f);
    // Just below the sea level: still the sea, its ground below 0.
    HorizonCell shallow;
    shallow.height = -0.001f;
    shallow.flags = ocean.flags;
    const HorizonTexel shore = terrain::unreal::horizonTexel(shallow, true);
    TEST_CHECK(terrain::unreal::isSeaTexel(shore) && terrain::unreal::texelGround(shore) < 0.0f);
    HorizonCell beach;
    beach.height = 0.0f;
    beach.flags = ocean.flags;
    TEST_CHECK(!terrain::unreal::isSeaTexel(terrain::unreal::horizonTexel(beach, true)));
    // A dry depression below the sea level, far from the sea: its ground, never the sea.
    HorizonCell depression = land;
    depression.height = -120.0f;
    const HorizonTexel basin = terrain::unreal::horizonTexel(depression, true);
    TEST_CHECK(!terrain::unreal::isSeaTexel(basin) && basin.height == -120.0f);
    // Without the sea, the ground keeps its height and is never taken for the sea.
    const HorizonTexel dry = terrain::unreal::horizonTexel(ocean, false);
    TEST_CHECK(dry.height == -42.0f && !terrain::unreal::isSeaTexel(dry));
    TEST_CHECK(terrain::unreal::texelGround(dry) == 0.0f);
    HorizonCell lake = land;
    lake.flags = terrain::kHorizonWater;
    TEST_CHECK(terrain::unreal::horizonTexel(lake, true) == ground);
}

// A texture of side x side texels, kTexelFloats floats each.
struct Texture {
    int side = 0;
    std::vector<float> texels;

    void write(const terrain::unreal::TexelRegion& region) {
        for (int row = 0; row < region.height; ++row) {
            for (int column = 0; column < region.width; ++column) {
                for (int f = 0; f < kTexelFloats; ++f) {
                    const auto source =
                        static_cast<std::size_t>((row * region.width + column) * kTexelFloats + f);
                    const auto target = static_cast<std::size_t>(
                        ((region.z + row) * side + region.x + column) * kTexelFloats + f);
                    texels[target] = region.data[source];
                }
            }
        }
    }

    [[nodiscard]] HorizonTexel at(terrain::HorizonIndex slot) const {
        const auto first = static_cast<std::size_t>((slot.z * side + slot.x) * kTexelFloats);
        return {texels[first], texels[first + 1], texels[first + 2], texels[first + 3]};
    }
};

// Regions of rectangles across the wrap: every sample at its slot, with its floats.
void testRegions() {
    constexpr int kSide = 17;
    for (const HorizonRect rect : {HorizonRect{3, 4, 9, 6}, HorizonRect{-5, 14, 6, 22},
                                   HorizonRect{30, -40, 47, -23}, HorizonRect{-1, -1, 1, 1}}) {
        std::vector<float> data;
        for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
            for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
                data.insert(data.end(), {static_cast<float>(i), static_cast<float>(k), 1.0f, 2.0f});
            }
        }
        Texture texture{kSide, std::vector<float>(kSide * kSide * kTexelFloats, -1.0f)};
        const auto regions = terrain::unreal::texelRegions(3, kSide, rect, data);
        TEST_CHECK(!regions.empty() && regions.size() <= 4u);
        for (const auto& region : regions) {
            TEST_CHECK(region.level == 3);
            texture.write(region);
        }
        bool placed = true;
        for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
            for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
                const HorizonTexel texel = texture.at(terrain::slotOf({i, k}, kSide));
                placed = placed && texel.height == static_cast<float>(i) &&
                         texel.gradX == static_cast<float>(k);
            }
        }
        TEST_CHECK(placed);
    }
    bool refused = false;
    try {
        static_cast<void>(terrain::unreal::texelRegions(0, kSide, {0, 0, 2, 2}, std::vector<float>(3)));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

// The textures written by the epochs hold the texel of every sample of every window at its slot.
bool texturesMatch(const std::vector<Texture>& textures, const terrain::HorizonClipmap& clipmap) {
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        const HorizonRect window = level.window();
        for (std::int64_t k = window.z0; k < window.z1; ++k) {
            for (std::int64_t i = window.x0; i < window.x1; ++i) {
                const HorizonTexel expected = terrain::unreal::horizonTexel(level.at({i, k}), true);
                if (!(textures[static_cast<std::size_t>(n)].at(terrain::slotOf({i, k}, level.side())) ==
                      expected)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void testWalk() {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    terrain::unreal::HorizonEpochRunner runner(
        sampler, [](std::function<void()> job) { job(); },
        [](const terrain::HorizonPiece& piece) { return terrain::unreal::pieceTexels(piece, true); });
    const int side = terrain::samplesPerSide(params);
    std::vector<Texture> textures(
        static_cast<std::size_t>(params.levels),
        Texture{side, std::vector<float>(static_cast<std::size_t>(side * side * kTexelFloats), -7.0f)});
    const terrain::WorldSize size;
    double x = size.x / 2.0 - 1'500.0;
    double z = size.z / 2.0 - 900.0;
    bool match = true;
    for (int step = 0; step < 60; ++step) {
        const terrain::unreal::EpochCommit commit = runner.update(x, z);
        for (const terrain::unreal::PreparedPiece& piece : commit.pieces) {
            for (const auto& region :
                 terrain::unreal::texelRegions(piece.level, side, piece.rect, piece.data)) {
                textures[static_cast<std::size_t>(region.level)].write(region);
            }
        }
        match = match && texturesMatch(textures, runner.clipmap());
        x += 53.0;
        z += 41.0;
    }
    TEST_CHECK(match);
    TEST_CHECK(x > size.x / 2.0 && z > size.z / 2.0); // across the corner of the world
}

// Cells drawn by the triangles of a grid (centroids), each by two triangles facing up.
std::map<std::pair<int, int>, int> cellsOf(const GridMesh& mesh, std::size_t triangles,
                                           bool& up) {
    std::map<std::pair<int, int>, int> cells;
    for (std::size_t t = 0; t < triangles; ++t) {
        const auto& a = mesh.vertices[static_cast<std::size_t>(mesh.triangles[3 * t])];
        const auto& b = mesh.vertices[static_cast<std::size_t>(mesh.triangles[3 * t + 1])];
        const auto& c = mesh.vertices[static_cast<std::size_t>(mesh.triangles[3 * t + 2])];
        // Unreal draws the front of (a, b, c) on the side of (c - a) x (b - a), Z = k up.
        up = up && (c.i - a.i) * (b.k - a.k) - (c.k - a.k) * (b.i - a.i) > 0.0f;
        const int i = static_cast<int>((a.i + b.i + c.i) / 3.0f);
        const int k = static_cast<int>((a.k + b.k + c.k) / 3.0f);
        ++cells[{i, k}];
    }
    return cells;
}

void testGrids() {
    constexpr int kCells = 64;
    const CellRect hole = terrain::unreal::centeredSquare(kCells, 17);
    const terrain::unreal::BodyTopology ring = terrain::unreal::bodyTopology(kCells, hole, true);
    const GridMesh body = terrain::unreal::bodyGrid(ring, 12.0);
    TEST_CHECK(body.vertices.size() == ring.vertexCount() - 1);
    TEST_CHECK(body.triangles == ring.triangles);
    TEST_CHECK(body.vertices[5].kind == GridVertexKind::Sample);
    TEST_CHECK(body.vertices[static_cast<std::size_t>(ring.skirtFirst)].kind == GridVertexKind::Skirt);
    const auto sea = static_cast<std::size_t>(ring.seaFirst);
    TEST_CHECK((body.vertices[sea] == terrain::unreal::GridVertex{20.0f, 20.0f, GridVertexKind::Sea}));
    TEST_CHECK(body.vertices[sea + 3].i == 44.0f);
    bool samples = true;
    for (std::size_t v = 0; v < ring.samples.size(); ++v) {
        samples = samples && body.vertices[v].i == static_cast<float>(ring.samples[v] % 65) &&
                  body.vertices[v].k == static_cast<float>(ring.samples[v] / 65);
    }
    TEST_CHECK(samples);
    const CellRect square = terrain::unreal::centeredSquare(kCells, kCells / 4 + 1);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const GridMesh trim = terrain::unreal::trimGrid(kCells, dx, dz);
            const CellRect finer{16 + dx, 16 + dz, 48 + dx, 48 + dz};
            TEST_CHECK(terrain::unreal::trimVariant(finer, kCells) == (dx + 1) + 3 * (dz + 1));
            bool up = true;
            const auto cells = cellsOf(trim, trim.triangleCount(), up);
            TEST_CHECK(up);
            std::size_t expected = 0;
            bool twice = true;
            for (int k = square.k0; k < square.k1; ++k) {
                for (int i = square.i0; i < square.i1; ++i) {
                    if (!finer.contains(i, k)) {
                        ++expected;
                        const auto found = cells.find({i, k});
                        twice = twice && found != cells.end() && found->second == 2;
                    }
                }
            }
            TEST_CHECK(twice && cells.size() == expected);
        }
    }
    for (const CellRect off : {CellRect{18, 16, 50, 48}, CellRect{16, 16, 47, 48}}) {
        bool refused = false;
        try {
            static_cast<void>(terrain::unreal::trimVariant(off, kCells));
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        TEST_CHECK(refused);
    }
}

} // namespace

int main() {
    testTexels();
    testRegions();
    testWalk();
    testGrids();
    return test::result();
}
