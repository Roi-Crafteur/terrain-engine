// The walls of the mountains in three dimensions in the streamed world (phase 12A): by the K2 the
// faces stand on many columns, tall ones among them, some with caps and undercuts; on the LOD 0
// tiles around the tallest face the near-vertical triangles (85 degrees and more) cover far more
// than on the tiles of the relief alone; and no rock floats there.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::RockColumn;
using terrain::RockFace;

// The foot of the highest wall by the K2 (mountain_freeze_test's window "highest wall").
constexpr double kWallX = 416'017.0;
constexpr double kWallZ = -11'414'806.0;
constexpr double kSearch = 300.0; // meters around it
constexpr double kStep = 4.0;
// The cosine of 85 degrees: the triangles whose normal rises less stand 85 degrees or more.
constexpr double kCos85 = 0.0871557;

struct Found {
    std::size_t columns = 0; // with a face
    std::size_t tall = 0;    // faces of 20 m or more
    std::size_t caps = 0;    // caps of 1.5 m or more
    std::size_t undercuts = 0;
    double x = 0.0; // the middle of the tallest face
    double z = 0.0;
    float height = 0.0f;
};

Found searchFaces(const terrain::Heightfield& field) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (double dz = -kSearch; dz <= kSearch; dz += kStep) {
        for (double dx = -kSearch; dx <= kSearch; dx += kStep) {
            xs.push_back(kWallX + dx);
            zs.push_back(kWallZ + dz);
        }
    }
    std::vector<terrain::HeightSample> samples(xs.size());
    std::vector<RockColumn> rock(xs.size());
    field.samplesAt(xs, zs, samples, rock);
    Found out;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out.columns += rock[i].faceCount > 0 ? 1 : 0;
        for (std::size_t k = 0; k < rock[i].faceCount; ++k) {
            const RockFace& f = rock[i].faces[k];
            const float height = (f.top - f.foot) * f.weight;
            out.tall += height >= 20.0f ? 1 : 0;
            out.caps += f.cap >= 1.5f ? 1 : 0;
            out.undercuts += f.undercut >= 1.5f ? 1 : 0;
            if (height > out.height) {
                out = {out.columns, out.tall, out.caps, out.undercuts,
                       xs[i] - f.distance * f.airX, zs[i] - f.distance * f.airZ, height};
            }
        }
    }
    return out;
}

void testFacesByK2(const Found& found) {
    std::printf("by the K2: %zu of %d columns with a face, %zu faces of 20 m and more, %zu caps "
                "and %zu undercuts of 1.5 m and more; the tallest %.0f m at (%.0f, %.0f)\n",
                found.columns, 151 * 151, found.tall, found.caps, found.undercuts,
                static_cast<double>(found.height), found.x, found.z);
    TEST_CHECK(found.columns > 1'000);
    TEST_CHECK(found.tall > 200);
    TEST_CHECK(found.caps > 20 && found.undercuts > 5);
    TEST_CHECK(found.height > 50.0f);
}

// The area of the triangles of the LOD 0 tiles around a point that stand 85 degrees or more.
double verticalArea(const terrain::TerrainTiles& tiles, double x, double z) {
    const terrain::ChunkCoord center = terrain::worldToChunk(x, z);
    double area = 0.0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const terrain::TileData t = tiles.generate({{{center.x + dx, center.z + dz}, 0}, 0});
            const auto& v = t.mesh.vertices;
            for (std::size_t i = 0; i + 2 < t.mesh.indices.size(); i += 3) {
                const terrain::Vec3f& a = v[t.mesh.indices[i]];
                const terrain::Vec3f& b = v[t.mesh.indices[i + 1]];
                const terrain::Vec3f& c = v[t.mesh.indices[i + 2]];
                const std::array<double, 3> u{b.x - a.x, b.y - a.y, b.z - a.z};
                const std::array<double, 3> w{c.x - a.x, c.y - a.y, c.z - a.z};
                const std::array<double, 3> n{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
                                              u[0] * w[1] - u[1] * w[0]};
                const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (length > 0.0 && std::abs(n[1]) / length < kCos85) {
                    area += 0.5 * length;
                }
            }
        }
    }
    return area;
}

void testVerticalTriangles(const Found& found) {
    const terrain::TerrainTiles rock(terrain::kDefaultWorldSeed);
    const terrain::TerrainTiles relief(terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {},
                                       nullptr, false);
    const double withRock = verticalArea(rock, found.x, found.z);
    const double alone = verticalArea(relief, found.x, found.z);
    std::printf("triangles of 85 degrees and more around the tallest face: %.0f m2 (%.0f m2 on "
                "the relief alone)\n",
                withRock, alone);
    TEST_CHECK(withRock > 300.0 && withRock > 5.0 * alone);
}

// The solid samples of a box of columns every meter and heights every half meter around the
// tallest face, all of them reached from the bottom of the box or from its sides (the rock goes
// on beyond them) through their six neighbors.
void testNoFloatingRock(const terrain::Heightfield& field, const Found& found) {
    constexpr int kSide = 48;
    constexpr double kDy = 0.5;
    std::vector<double> xs;
    std::vector<double> zs;
    const double x0 = std::floor(found.x) - kSide / 2;
    const double z0 = std::floor(found.z) - kSide / 2;
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            xs.push_back(x0 + i);
            zs.push_back(z0 + k);
        }
    }
    std::vector<terrain::HeightSample> samples(xs.size());
    std::vector<RockColumn> rock(xs.size());
    field.samplesAt(xs, zs, samples, rock);
    float low = 1e30f;
    float high = -1e30f;
    for (const RockColumn& c : rock) {
        const terrain::RockBounds b = terrain::rockBounds(c);
        low = std::min(low, b.low);
        high = std::max(high, b.high);
    }
    const double bottom = std::floor(low) - 2.0;
    const auto levels = static_cast<int>((high + 2.0 - bottom) / kDy) + 1;
    const auto index = [&](int i, int j, int k) {
        return static_cast<std::size_t>((j * kSide + k) * kSide + i);
    };
    std::vector<char> solid(static_cast<std::size_t>(levels * kSide * kSide), 0);
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            const RockColumn& c = rock[static_cast<std::size_t>(k * kSide + i)];
            for (int j = 0; j < levels; ++j) {
                solid[index(i, j, k)] = terrain::rockDensity(c, bottom + j * kDy, {}) < 0.0f;
            }
        }
    }
    std::vector<std::array<int, 3>> stack;
    std::vector<char> reached(solid.size(), 0);
    const auto visit = [&](int i, int j, int k) {
        if (i >= 0 && i < kSide && k >= 0 && k < kSide && j >= 0 && j < levels &&
            solid[index(i, j, k)] && !reached[index(i, j, k)]) {
            reached[index(i, j, k)] = 1;
            stack.push_back({i, j, k});
        }
    };
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            for (int j = 0; j < levels; ++j) {
                if (j == 0 || i == 0 || k == 0 || i == kSide - 1 || k == kSide - 1) {
                    visit(i, j, k);
                }
            }
        }
    }
    while (!stack.empty()) {
        const auto [i, j, k] = stack.back();
        stack.pop_back();
        visit(i + 1, j, k);
        visit(i - 1, j, k);
        visit(i, j + 1, k);
        visit(i, j - 1, k);
        visit(i, j, k + 1);
        visit(i, j, k - 1);
    }
    std::size_t floating = 0;
    std::size_t total = 0;
    for (std::size_t n = 0; n < solid.size(); ++n) {
        total += solid[n] ? 1 : 0;
        floating += solid[n] && !reached[n] ? 1 : 0;
    }
    std::printf("around the tallest face: %zu solid samples, %zu floating\n", total, floating);
    TEST_CHECK(total > 0 && floating == 0);
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const Found found = searchFaces(field);
    testFacesByK2(found);
    testVerticalTriangles(found);
    testNoFloatingRock(field, found);
    return test::result();
}
