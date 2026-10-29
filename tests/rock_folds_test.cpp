// The overhanging folds of the walls (phase 12A): the band of shadow along a fold stays whole,
// whatever its angle with the lattice. The faces of a weight between kFullLow and 1 draw their
// overhangs and grain in part and lean on the relief of each column: along a fold such faces
// break the band of shadow into saw teeth (the cells catch the thin reach of a lip at one column
// out of two), and its tip into dashes.
// - By the K2, at LOD 0 and LOD 1: the faces with a cap (a reach of 1.5 cells at least, from
//   kFullLow), at least kLeastCaps of them (6 372 and 455 when the folds broke: a tenth fewer at
//   most), at most kMostPartial of them at a weight under 1 (then 31% and 37%; now 14% and 13%).
// - The fold of the view of the black slits (from (416 080, -11 414 957), its lip by
//   (416 086, -11 414 913)), its LOD 0 tiles drawn as the zoom of the review drew them: along the
//   band of shadow, the light changes by kRoughest (of 255) on average over 4 pixels at most (0.56
//   with the saw teeth; now 0.41).
// - Lips across a synthetic tile at 0 to 45 degrees from its lattice, with grain and a joint,
//   whole and fading along: the width of their ceiling (the triangles facing down) never falls
//   to nothing along them.
#include "rock_tile.hpp"
#include "tile_lattice.hpp"
#include "tile_normals.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/io/mesh_view.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using terrain::Mesh;
using terrain::RockColumn;
using terrain::RockFace;
using terrain::Vec3f;

constexpr double kCenterX = 416'000.0;
constexpr double kCenterZ = -11'414'800.0;
constexpr int kHalf = 300; // meters
constexpr std::size_t kRows = 64;
constexpr std::array<std::size_t, 2> kLeastCaps = {5'700, 410};
constexpr double kMostPartial = 0.2;
constexpr float kFullLow = 0.75f;
constexpr float kCapCells = 1.5f;
constexpr double kRoughest = 0.5;
// A ceiling faces down by kDown at least (cosine with -y); its width falls to nothing under kEmpty
// of its median, sampled every kAlong meters.
constexpr double kDown = 0.5;
constexpr double kEmpty = 0.15;
constexpr double kAlong = 0.1;

struct Caps {
    std::size_t faces = 0;
    std::size_t partial = 0;
};

Caps capsByTheK2(const terrain::Heightfield& field, float cell) {
    const auto n = static_cast<std::size_t>(2 * kHalf / cell) + 1;
    Caps out;
    for (std::size_t first = 0; first < n; first += kRows) {
        std::vector<double> xs;
        std::vector<double> zs;
        for (std::size_t k = first; k < std::min(n, first + kRows); ++k) {
            for (std::size_t i = 0; i < n; ++i) {
                xs.push_back(kCenterX - kHalf + cell * static_cast<double>(i));
                zs.push_back(kCenterZ - kHalf + cell * static_cast<double>(k));
            }
        }
        std::vector<terrain::HeightSample> samples(xs.size());
        std::vector<RockColumn> rock(xs.size());
        field.samplesAt(xs, zs, samples, rock, terrain::kRunCells * cell);
        for (const RockColumn& c : rock) {
            for (std::size_t f = 0; f < c.faceCount; ++f) {
                const RockFace& face = c.faces[f];
                if (face.cap >= kCapCells * cell && face.weight >= kFullLow) {
                    ++out.faces;
                    out.partial += face.weight < 0.999f ? 1 : 0;
                }
            }
        }
    }
    return out;
}

void testCapsByTheK2(const terrain::Heightfield& field) {
    for (const int lod : {0, 1}) {
        const Caps caps = capsByTheK2(field, static_cast<float>(1 << lod));
        const double share = static_cast<double>(caps.partial) /
                             static_cast<double>(std::max<std::size_t>(1, caps.faces));
        std::printf("LOD %d by the K2: %zu faces with a cap, %.1f%% of them at a weight under 1\n",
                    lod, caps.faces, 100.0 * share);
        TEST_CHECK(caps.faces >= kLeastCaps[static_cast<std::size_t>(lod)]);
        TEST_CHECK(share <= kMostPartial);
    }
}

// The light of an image at (x, y) (bilinear between its pixels, clamped to it).
double lightAt(const terrain::Image& image, double x, double y) {
    x = std::clamp(x, 0.0, image.width - 1.001);
    y = std::clamp(y, 0.0, image.height - 1.001);
    const auto i = static_cast<int>(x);
    const auto j = static_cast<int>(y);
    const auto at = [&](int a, int b) {
        const auto width = static_cast<std::size_t>(image.width);
        const std::size_t p = 3 * (static_cast<std::size_t>(b) * width + static_cast<std::size_t>(a));
        return 0.299 * image.pixels[p] + 0.587 * image.pixels[p + 1] + 0.114 * image.pixels[p + 2];
    };
    const double fx = x - i;
    const double fy = y - j;
    return (1.0 - fy) * ((1.0 - fx) * at(i, j) + fx * at(i + 1, j)) +
           fy * ((1.0 - fx) * at(i, j + 1) + fx * at(i + 1, j + 1));
}

// How much the light of the dark pixels (under 80) of a part of an image changes over 4 pixels
// along (dx, dy), on average.
double roughness(const terrain::Image& image, const std::array<int, 4>& part, double dx,
                 double dy) {
    const double length = std::hypot(dx, dy);
    double sum = 0.0;
    std::size_t count = 0;
    for (int y = part[1]; y < part[3]; ++y) {
        for (int x = part[0]; x < part[2]; ++x) {
            const double here = lightAt(image, x, y);
            if (here < 80.0) {
                const double there = lightAt(image, x + 4.0 * dx / length, y + 4.0 * dy / length);
                sum += std::abs(there - here);
                ++count;
            }
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

void testFoldOfTheView() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    std::vector<terrain::TileData> data;
    for (std::int32_t z = -713'435; z <= -713'430; ++z) {
        for (std::int32_t x = 26'003; x <= 26'007; ++x) {
            data.push_back(tiles.generate({{{x, z}, 0}, 0}));
        }
    }
    std::vector<terrain::ViewMesh> meshes;
    for (const terrain::TileData& t : data) {
        meshes.push_back({&t.mesh, t.originX, t.originY, t.originZ, {}, false, {}});
    }
    terrain::ViewParams view;
    view.eyeX = 416'080.0;
    view.eyeZ = -11'414'957.4;
    view.eyeHeight = tiles.groundAt(view.eyeX, view.eyeZ) + 22.0;
    view.heading = 171.04 / 360.0;
    view.tilt = 0.727;
    view.fieldOfView = 15.0 / 360.0;
    view.width = 1'000;
    view.height = 700;
    const auto pixels = static_cast<std::size_t>(view.width * view.height);
    const terrain::Image sky{view.width, view.height, 3, std::vector<std::uint8_t>(3 * pixels, 255)};
    const terrain::Image image = terrain::renderMeshView(meshes, sky, view, 4, {0.42, 0.8, false});
    // The band of shadow of the fold runs down to the right, (0.57, 0.82) across the image.
    const double rough = roughness(image, {150, 0, 850, 500}, 0.57, 0.82);
    std::printf("the fold of the view: the light of its band of shadow changes by %.3f over 4 "
                "pixels along it\n",
                rough);
    TEST_CHECK(rough > 0.0 && rough <= kRoughest);
}

// The width across (tangentX, tangentZ) of the ceiling of a fold (the triangles of a mesh facing
// down within a box, seen from under), every kAlong meters along it; positions relative to the box
// center. A lip drawn at one column out of two leaves its ceiling as a row of teeth: its width
// falls to nothing between them.
std::vector<double> ceilingWidths(const Mesh& m, const std::array<double, 3>& origin,
                                  const std::array<double, 4>& box, double tangentX,
                                  double tangentZ, double half) {
    std::vector<double> width(static_cast<std::size_t>(2.0 * half / kAlong) + 1, 0.0);
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        std::array<std::array<double, 3>, 3> p{};
        std::array<std::array<double, 2>, 3> q{};
        for (std::size_t j = 0; j < 3; ++j) {
            const Vec3f& v = m.vertices[m.indices[t + j]];
            p[j] = {origin[0] + v.x - box[0], origin[1] + v.y, origin[2] + v.z - box[1]};
            q[j] = {p[j][0] * tangentX + p[j][2] * tangentZ,
                    p[j][2] * tangentX - p[j][0] * tangentZ};
        }
        const std::array<double, 3> u{p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const std::array<double, 3> v{p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const std::array<double, 3> w{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                      u[0] * v[1] - u[1] * v[0]};
        const double length = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        const double y = (p[0][1] + p[1][1] + p[2][1]) / 3.0;
        if (!(length > 0.0) || w[1] / length > -kDown || y < box[2] || y > box[3]) {
            continue;
        }
        for (std::size_t s = 0; s < width.size(); ++s) {
            const double a = -half + kAlong * static_cast<double>(s);
            double low = 1e30;
            double high = -1e30;
            for (std::size_t j = 0; j < 3; ++j) {
                const auto& e0 = q[j];
                const auto& e1 = q[(j + 1) % 3];
                if ((e0[0] - a) * (e1[0] - a) <= 0.0 && e0[0] != e1[0]) {
                    const double c = e0[1] + (e1[1] - e0[1]) * (a - e0[0]) / (e1[0] - e0[0]);
                    low = std::min(low, c);
                    high = std::max(high, c);
                }
            }
            width[s] += high > low && std::abs(low) <= half ? high - low : 0.0;
        }
    }
    return width;
}

// The samples of a ceiling narrower than kEmpty of its median between two wider ones.
std::size_t gapsIn(const std::vector<double>& width) {
    std::vector<double> sorted = width;
    std::sort(sorted.begin(), sorted.end());
    const double least = kEmpty * sorted[sorted.size() / 2];
    std::size_t first = width.size();
    std::size_t last = 0;
    for (std::size_t i = 0; i < width.size(); ++i) {
        if (width[i] >= least && width[i] > 0.0) {
            first = std::min(first, i);
            last = i;
        }
    }
    std::size_t gaps = 0;
    for (std::size_t i = first; i < last; ++i) {
        gaps += width[i] < least ? 1 : 0;
    }
    return gaps;
}

// A column of a synthetic tile crossed at angle radians from its x axis by a face standing at its
// middle (8, 8), its air toward -x turned by the angle, its relief climbing the riser. Fading, the
// weight of the face runs from 1 down to 0.75 over 10 m, through the middle of the tile, toward
// fading radians from along the face; its overhangs, grain and joint with it (as wall_faces draws
// them).
RockColumn syntheticColumn(int i, int k, double angle, const RockFace& face, double fading) {
    constexpr double kRun = 12.0;
    const double airX = -std::cos(angle);
    const double airZ = -std::sin(angle);
    const double d = (i - 8.0) * airX + (k - 8.0) * airZ;
    const double along = (i - 8.0) * (std::cos(fading) * airZ + std::sin(fading) * airX) -
                         (k - 8.0) * (std::cos(fading) * airX - std::sin(fading) * airZ);
    RockColumn c;
    c.ground = static_cast<float>(std::clamp(0.5 * face.top - face.top * d / kRun, 0.0,
                                             static_cast<double>(face.top)));
    c.uncarved = c.ground;
    if (std::abs(d) <= 0.5 * kRun) {
        RockFace f = face;
        f.distance = static_cast<float>(d);
        f.run = static_cast<float>(kRun);
        f.airX = static_cast<float>(airX);
        f.airZ = static_cast<float>(airZ);
        if (fading >= 0.0) {
            f.weight = static_cast<float>(1.0 - 0.25 * std::clamp((along + 5.0) / 10.0, 0.0, 1.0));
            f.cap *= f.weight;
            f.undercut *= f.weight;
            f.grain *= f.weight;
            f.joint *= f.weight;
        }
        c.add(f);
    }
    return c;
}

// The mesh of the synthetic tile of a lip (level 0, its normals as a tile lights them).
Mesh syntheticLip(double angle, const RockFace& face, double fading) {
    terrain::tile::TileColumns columns;
    constexpr int kSide = terrain::kGridSamples;
    columns.continentalness.assign(static_cast<std::size_t>(kSide * kSide), 0.5f);
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            columns.rock.push_back(syntheticColumn(i, k, angle, face, fading));
            columns.heights.push_back(columns.rock.back().ground);
        }
    }
    for (int side = 0; side < 4; ++side) {
        for (int a = 0; a < kSide; ++a) {
            const int i = side == 0 ? -1 : (side == 1 ? kSide : a);
            const int k = side == 2 ? -1 : (side == 3 ? kSide : a);
            columns.ring.push_back(syntheticColumn(i, k, angle, face, fading));
            columns.ringHeights.push_back(columns.ring.back().ground);
        }
    }
    columns.maxHeight = face.top;
    const terrain::TileKey key{{0, 0}, 0};
    const terrain::tile::RockTile tile(columns, {key, 0}, terrain::tile::latticeOf(key));
    const terrain::DensityGrid grid = tile.levelDensity(0);
    Mesh mesh = terrain::marchingCubes(grid);
    tile.lightLevel(mesh, grid, 0);
    terrain::tile::agreeWithTriangles(mesh);
    return mesh;
}

void testSyntheticLips() {
    RockFace face;
    face.top = 12.0f;
    face.cap = 2.5f;
    face.capDepth = 5.0f;
    face.grain = 0.7f;
    face.joint = 0.6f;
    face.jointWidth = 5.0f;
    std::size_t gaps = 0;
    std::size_t lips = 0;
    for (const double fading : {-1.0, 0.0}) {
        for (const float shift : {0.0f, 2.2f, 4.1f}) {
            for (int degrees = 0; degrees <= 45; degrees += 5) {
                face.grainShift = shift;
                const double angle = degrees * std::numbers::pi / 180.0;
                const std::vector<double> width =
                    ceilingWidths(syntheticLip(angle, face, fading), {0.0, 0.0, 0.0},
                                  {8.0, 8.0, 0.0, 14.0}, -std::sin(angle), std::cos(angle), 5.0);
                gaps += gapsIn(width);
                lips += *std::max_element(width.begin(), width.end()) > 0.0 ? 1 : 0;
            }
        }
    }
    std::printf("synthetic lips at 0 to 45 degrees, whole and fading along: %zu of 60 with a "
                "ceiling, %zu samples narrower than %.0f%% of it within them\n",
                lips, gaps, 100.0 * kEmpty);
    TEST_CHECK(lips == 60);
    TEST_CHECK(gaps == 0);
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    testCapsByTheK2(field);
    testFoldOfTheView();
    testSyntheticLips();
    return test::result();
}
