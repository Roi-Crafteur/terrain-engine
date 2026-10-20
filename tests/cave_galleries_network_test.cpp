// The network of the galleries (Phase 8C-2): in a world of Tunnels regions on flat ground, on a
// 2D graph of the gallery axes over a 3 km square, one component spans the square and holds most
// of the network, and the two networks cross at many junctions, where the carved air of the
// galleries joins in 3D; in the Tunnels regions of the streamed world, the network spans more
// than a kilometer, the samples cut by the limits of the caves counted apart.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <queue>
#include <vector>

namespace {

using caves_test::kSeed;
using galleries_test::Component;
using galleries_test::Network;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveSampler;

constexpr float kFlatHeight = 300.0f;

// Carved air reached from (x, y, z) by a 6-connected flood fill of 1 m samples over the box of
// half side 24 m around (x, z), from y - 7 to y + 7: the lateral faces it reaches (bits 1, 2,
// 4, 8 for -x, +x, -z, +z).
unsigned facesReached(const CaveField& field, const terrain::CaveGround& ground, double x,
                      double y, double z) {
    constexpr int kHalf = 24;
    constexpr int kSide = 2 * kHalf + 1;
    constexpr int kHeight = 15;
    CaveSampler sampler(field, ground, terrain::CaveDetail{},
                        {x - kHalf, y - 8.0, z - kHalf, x + kHalf, y + 8.0, z + kHalf});
    std::vector<unsigned char> air(static_cast<std::size_t>(kSide * kSide * kHeight), 0);
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            const double px = x - kHalf + i;
            const double pz = z - kHalf + k;
            const float height = ground.height(px, pz);
            const CaveColumn column = sampler.column(px, pz, height);
            for (int j = 0; j < kHeight; ++j) {
                const double py = y - 7.0 + j;
                const float terrainDensity = caves_test::terrainDensity(py, height);
                const float carved = sampler.density(column, py, terrainDensity);
                air[static_cast<std::size_t>((j * kSide + k) * kSide + i)] =
                    caves_test::carvedAir(terrainDensity, carved) ? 1 : 0;
            }
        }
    }
    const auto at = [&](int i, int j, int k) {
        return static_cast<std::size_t>((j * kSide + k) * kSide + i);
    };
    std::vector<unsigned char> seen(air.size(), 0);
    std::queue<std::array<int, 3>> queue;
    if (air[at(kHalf, 7, kHalf)] != 0) {
        queue.push({kHalf, 7, kHalf});
        seen[at(kHalf, 7, kHalf)] = 1;
    }
    unsigned faces = 0;
    while (!queue.empty()) {
        const auto [i, j, k] = queue.front();
        queue.pop();
        faces |= (i == 0 ? 1U : 0U) | (i == kSide - 1 ? 2U : 0U) | (k == 0 ? 4U : 0U) |
                 (k == kSide - 1 ? 8U : 0U);
        constexpr std::array<std::array<int, 3>, 6> kSteps{
            {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        for (const auto& s : kSteps) {
            const int a = i + s[0];
            const int b = j + s[1];
            const int c = k + s[2];
            if (a < 0 || b < 0 || c < 0 || a >= kSide || b >= kHeight || c >= kSide ||
                air[at(a, b, c)] == 0 || seen[at(a, b, c)] != 0) {
                continue;
            }
            seen[at(a, b, c)] = 1;
            queue.push({a, b, c});
        }
    }
    return faces;
}

int faceCount(unsigned faces) {
    int count = 0;
    for (unsigned bit = 1; bit <= 8; bit <<= 1) {
        count += (faces & bit) != 0 ? 1 : 0;
    }
    return count;
}

// The network of a world of Tunnels regions on flat ground, over a 3 km square on a 2D graph of
// 4 m samples (the gallery axis within its half width, caves allowed at its center): one
// component spans the square, touching its 4 sides, and holds most of the network; the two
// networks cross at many junctions, where the carved air of the galleries joins in 3D.
void testNetwork() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Tunnels));
    const terrain::CaveGround ground = caves_test::flatGround(kFlatHeight);
    const Network net = galleries_test::scanNetwork(field, ground, 1000.0, -2000.0, 4.0, 750);
    const std::vector<Component> parts = galleries_test::components(net.open, net.size);
    const auto largest = std::max_element(parts.begin(), parts.end(), [](auto& a, auto& b) {
        return a.size < b.size;
    });
    const std::vector<Component> junctions = galleries_test::components(net.junction, net.size);
    const double share = largest == parts.end() ? 0.0
                                                : static_cast<double>(largest->size) /
                                                      static_cast<double>(net.openCount);
    std::printf("network: %zu open samples, %zu components, the largest %.1f%% touching sides "
                "%X; %zu junctions\n",
                net.openCount, parts.size(), 100.0 * share,
                largest == parts.end() ? 0U : largest->sides, junctions.size());
    TEST_CHECK(largest != parts.end() && largest->sides == 0xFU);
    TEST_CHECK(share >= 0.85);
    TEST_CHECK(junctions.size() >= 15);
    const double center = kFlatHeight - field.params().galleries.floorDepth +
                          field.params().galleries.halfHeight;
    int joined = 0;
    for (std::size_t n = 0; n < std::min<std::size_t>(junctions.size(), 3); ++n) {
        const auto at = static_cast<int>(junctions[n].first);
        const double x = net.x0 + (at % net.size) * net.step;
        const double z = net.z0 + (at / net.size) * net.step;
        const int faces = faceCount(facesReached(field, ground, x, center, z));
        std::printf("junction at (%.0f, %.0f): carved air reaches %d faces\n", x, z, faces);
        joined += faces >= 3 ? 1 : 0;
    }
    TEST_CHECK(joined == 3);
}

// In the streamed world of key 5 (the heightfield, without surface rivers), around the site of
// the first Tunnels region on land: the largest component of the network spans at least a
// kilometer. Samples where the limits of the caves (the roof, the deepest caves, the deep sea)
// close the center of a gallery are counted apart.
void testRealNetwork() {
    terrain::CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    const terrain::TerrainTiles tiles(kSeed, {}, {}, params);
    const CaveField& field = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    bool found = false;
    for (int n = 0; n < 169 && !found; ++n) {
        const terrain::CaveDistrict d = field.district(n % 13 - 6, n / 13 - 6, ground);
        const float height = ground.height(d.siteX, d.siteZ);
        if (d.type != CaveDistrictType::Tunnels || height < 20.0f || height > 400.0f) {
            continue;
        }
        found = true;
        constexpr int kSize = 400;
        constexpr double kStep = 4.0;
        const double half = kSize * kStep / 2.0;
        const Network net = galleries_test::scanNetwork(field, ground, d.siteX - half,
                                                        d.siteZ - half, kStep, kSize);
        const std::vector<Component> parts = galleries_test::components(net.open, net.size);
        double extent = 0.0;
        for (const Component& c : parts) {
            extent = std::max(extent, kStep * std::max(c.maxI - c.minI, c.maxK - c.minK));
        }
        std::printf("streamed world: Tunnels region at (%.0f, %.0f), ground %.0f m: %zu open "
                    "samples, %zu cut by the limits, %zu components, the widest %.0f m\n",
                    d.siteX, d.siteZ, static_cast<double>(height), net.openCount, net.cutCount,
                    parts.size(), extent);
        TEST_CHECK(net.openCount > 1000);
        TEST_CHECK(extent >= 1000.0);
    }
    TEST_CHECK(found);
}

} // namespace

int main() {
    testNetwork();
    testRealNetwork();
    return test::result();
}
