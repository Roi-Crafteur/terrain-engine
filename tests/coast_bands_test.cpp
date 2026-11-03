// The band channel of the rock (agent 33, rock_faces.hpp: RockBands): what the coarse tiles and
// the horizon do not draw of the faces and the spires (their beds, their notch, their cap), given
// by the core to every vertex of the tiles (TileData::rockBands) and to the samples of the horizon
// (horizon_bands.hpp), drawn by the materials (Look.Bands: coast_band_support.hpp, the same
// formula), on the default world with its planet file:
// - A face and a spire alone: its beds (one a bed, rising with the height, alike on two columns),
//   its notch from its floor, its cap from its underside; the heavier of two faces; none without
//   rock.
// - The notch of the notched cliff from a path toward its face, from kPathFar to kPathNear meters,
//   every kPathStep and at kPathNear: on the image of the tiles of the LOD Unreal draws there
//   (lodAt), the light of the rock in its notch kNotchDark of that of the rock just over it at
//   most at every step: the notch never disappears (d857a1b drew it at the LOD 0 and 1 only, by
//   the shape of its rock; no channel then). And on the samples of level 0 of the horizon there,
//   bands with a notch.
// - From one LOD to the next at the distances where Unreal switches them, the same eye: the mean
//   difference of the light of the rock of the face kLodLight of its mean at most, the normals
//   kLodTurn degrees apart at most in the median.
// - The beds of the stacks at 200 m (the views of the stacks: their LOD 2 tiles), under a sun
//   kSunHigh degrees high: their light, row by row over the middle of the stack, shows kBands
//   dark lines at least (each kLineDepth under the light around it), and changes kLayering times
//   more up the stack than across it (horizontal beds, not vertical streaks); the normals of its
//   pixels kSoft degrees off those of its faces at most on average (softnessOf: the vertices of a
//   spire lit by the gradient of the spire itself, RockTile::spireGradient; 6.8 to 17.1 degrees
//   with the gradients of the lattice smoothed over a cell or two, its edges soft, 3.8 to 11.3).
#include "coast_band_support.hpp"
#include "coast_image_support.hpp"
#include "coast_planet_support.hpp"
#include "coast_render_support.hpp"
#include "coast_rock_support.hpp"

#include "terrain/streaming/horizon_bands.hpp"
#include "terrain/streaming/lod.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <vector>

namespace {

using coast_image::Eye;
using coast_image::V3;
using coast_image::kHeight;
using coast_image::kWidth;

constexpr double kPathFar = 500.0;
constexpr double kPathNear = 5.0;
constexpr double kPathStep = 20.0;
constexpr double kNotchDark = 0.85;
constexpr double kSteep = 0.6;
constexpr double kLodLight = 0.12;
constexpr double kLodTurn = 12.0;
constexpr double kSunHigh = 8.0;
constexpr int kBands = 4;
constexpr double kLineDepth = 0.03;
constexpr double kLayering = 1.3;
constexpr double kSoft = 12.0;
constexpr double kOnFace = 1.0;
constexpr double kOnFoot = 6.0;
constexpr double kAcross = 0.05;

void testAlone() {
    terrain::RockColumn c;
    terrain::RockFace f;
    f.foot = 10.0f;
    f.top = 50.0f;
    f.grain = 0.6f;
    f.undercut = 1.0f;
    f.undercutHeight = 3.0f;
    f.cap = 1.0f;
    f.capDepth = 2.0f;
    c.add(f);
    const terrain::RockBands foot = terrain::rockBands(c, 10.0);
    const terrain::RockBands top = terrain::rockBands(c, 50.0);
    TEST_CHECK(foot.layered == 1.0f);
    TEST_CHECK(top.strata - foot.strata > 3.0f && top.strata - foot.strata < 9.0f); // 6.5 m beds
    // The same base for the columns around: the beds of two columns whose grain shifts a little
    // by a meter apart at most.
    terrain::RockColumn next;
    terrain::RockFace shifted = f;
    shifted.grainShift = 0.3f;
    next.add(shifted);
    TEST_CHECK(std::abs(terrain::rockBands(next, 30.0).strata -
                        terrain::rockBands(c, 30.0).strata) < 0.2f);
    float before = -1.0f;
    for (double y = 10.0; y <= 50.0; y += 0.25) {
        const float s = terrain::rockBands(c, y).strata;
        TEST_CHECK(s > before);
        before = s;
    }
    const terrain::RockBands notch = terrain::rockBands(c, 11.0);
    TEST_CHECK(notch.notch == 1.0f && notch.notchHeight == 3.0f);
    const terrain::RockBands cap = terrain::rockBands(c, 49.0);
    TEST_CHECK(cap.cap == 1.0f && cap.capHeight == 2.0f);
    terrain::RockFace heavier = f;
    heavier.weight = 2.0f;
    heavier.undercut = 0.0f;
    c.add(heavier);
    TEST_CHECK(terrain::rockBands(c, 11.0).notchHeight == 0.0f);
    TEST_CHECK(terrain::rockBands(terrain::RockColumn{}, 11.0) == terrain::RockBands{});
    // A spire: its beds, its notch on its sea side only.
    const terrain::CoastStack s =
        coast_planet::field().stacksIn(coast_rock::kStack.x - 50.0, coast_rock::kStack.z - 50.0,
                                       coast_rock::kStack.x + 50.0, coast_rock::kStack.z + 50.0)
            .front();
    int notched = 0, plain = 0;
    for (int a = 0; a < 16; ++a) {
        const double t = a * 3.14159265358979323846 / 8.0;
        const terrain::RockColumn r = coast_planet::field().rockAt(
            s.x + 0.4 * s.radius * std::cos(t), s.z + 0.4 * s.radius * std::sin(t));
        const terrain::RockBands b = terrain::rockBands(r, 1.0);
        notched += b.notchHeight > 0.0f ? 1 : 0;
        plain += r.spire.radius > 0.0f && b.notchHeight == 0.0f ? 1 : 0;
        TEST_CHECK(terrain::rockBands(r, 30.0).strata > terrain::rockBands(r, 10.0).strata);
    }
    std::printf("a spire: its notch toward %d of 16 directions, none toward %d\n", notched, plain);
    TEST_CHECK(notched > 0 && plain > 0);
}

// The LOD Unreal draws at d meters (the rings of its tiles, TerrainWorld.h: 64, 128, 256 m).
int lodAt(double d) {
    return d < 64.0 ? 0 : d < 128.0 ? 1 : d < 256.0 ? 2 : 3;
}

// The tiles of a LOD around (x, z), within reach meters.
std::vector<terrain::TileData> tilesAround(double x, double z, int lod, double reach) {
    std::map<std::pair<std::int32_t, std::int32_t>, terrain::TileData> found;
    const double step = terrain::kChunkSizeMeters;
    for (double dz = -reach; dz <= reach; dz += step) {
        for (double dx = -reach; dx <= reach; dx += step) {
            const terrain::ChunkCoord chunk{
                static_cast<std::int32_t>(std::floor((x + dx) / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor((z + dz) / terrain::kChunkSizeMeters))};
            const terrain::TileKey key = terrain::tileContaining(chunk, lod);
            if (!found.contains({key.origin.x, key.origin.z})) {
                found.emplace(std::pair{key.origin.x, key.origin.z},
                              coast_planet::tiles().generate({key, 0}));
            }
        }
    }
    std::vector<terrain::TileData> out;
    for (auto& [key, data] : found) {
        out.push_back(std::move(data));
    }
    return out;
}

// The light of the rock of the face in its notch, and just over it (as high as the notch), over
// the image of the tiles: their means.
std::array<double, 2> notchLight(const coast_bands::TileImage& img, bool bands, double low,
                                 double high) {
    const std::vector<double> l = img.luminance(bands);
    double in = 0.0, over = 0.0, nIn = 0.0, nOver = 0.0;
    const double reach = high - low;
    for (std::size_t at = 0; at < l.size(); ++at) {
        // The rock of the face: steep (not the platform at its foot, nor the roof of the notch).
        if (l[at] < 0.0 || img.rock[at] < 0.5 || std::abs(img.normal[at].y) > kSteep) {
            continue;
        }
        const double y = img.height[at];
        if (y > low + 0.2 * reach && y < high - 0.2 * reach) {
            in += l[at];
            nIn += 1.0;
        } else if (y > high + 0.5 && y < high + reach) {
            over += l[at];
            nOver += 1.0;
        }
    }
    return {nIn > 0.0 ? in / nIn : 1.0, nOver > 0.0 ? over / nOver : 1.0};
}

void testPath() {
    const std::vector<coast_render::Point> line = coast_render::lineOf(coast_rock::kCliff, 20.0);
    TEST_CHECK(line.size() > 20);
    if (line.size() <= 20) {
        return;
    }
    const coast_render::Point& p = line[line.size() / 2];
    const terrain::RockBands at = terrain::rockBands(coast_planet::field().rockAt(p.x, p.z), p.foot);
    const double low = p.foot - static_cast<double>(at.notch);
    const double high = low + static_cast<double>(at.notchHeight);
    std::map<int, std::vector<terrain::TileData>> lods;
    double worst = 0.0, worstPlain = 0.0;
    const double heading = std::atan2(-p.nx, p.nz) * 180.0 / 3.14159265358979323846;
    std::vector<double> distances;
    for (double d = kPathFar; d >= kPathNear + kPathStep; d -= kPathStep) {
        distances.push_back(d);
    }
    distances.push_back(kPathNear);
    for (const double d : distances) {
        const int lod = lodAt(d);
        if (!lods.contains(lod)) {
            lods[lod] = tilesAround(p.x, p.z, lod, 40.0);
        }
        // The eye a meter over the middle of the notch, its view up to its top (as the view 4 at
        // 5 m), wider near the face.
        const double eyeY = 0.5 * (low + high) + 1.0;
        const double pitch = std::atan2(high - eyeY, d) * 180.0 / 3.14159265358979323846;
        const Eye eye{p.x + d * p.nx, p.z + d * p.nz, eyeY, heading, pitch, d < 30.0 ? 90.0 : 50.0};
        const coast_bands::TileImage img =
            coast_bands::renderTiles(lods[lod], eye, coast_image::sunOf(eye, 0.0, 30.0));
        const std::array<double, 2> with = notchLight(img, true, low, high);
        const std::array<double, 2> without = notchLight(img, false, low, high);
        worst = std::max(worst, with[0] / with[1]);
        worstPlain = std::max(worstPlain, without[0] / without[1]);
        std::printf("  %.0f m (LOD %d): %.2f (%.2f without the bands)\n", d, lod,
                    with[0] / with[1], without[0] / without[1]);
    }
    std::printf("the notched cliff, its notch from %.1f to %.1f m, from %.0f to %.0f m away: its "
                "light at most %.2f of that over it (%.2f without the bands)\n",
                low, high, kPathFar, kPathNear, worst, worstPlain);
    TEST_CHECK(worst <= kNotchDark);
    // The horizon there: bands with a notch on its samples by the face.
    const coast_render::Level0 l = coast_render::level0Around(line);
    const std::vector<terrain::RockBands> bands = terrain::horizonBands(
        coast_planet::field(), terrain::HorizonParams{}, 0, l.rect, l.cells);
    int rock = 0, notched = 0;
    for (const terrain::RockBands& b : bands) {
        rock += b.layered > 0.0f ? 1 : 0;
        notched += b.notchHeight > 0.0f ? 1 : 0;
    }
    std::printf("its samples of level 0 of the horizon: %d with beds, %d with a notch\n", rock,
                notched);
    TEST_CHECK(rock > 0 && notched > 0);
}

// From one LOD to the next at the distances where Unreal switches them.
void testLods() {
    const std::vector<coast_render::Point> line = coast_render::lineOf(coast_rock::kCliff, 20.0);
    if (line.size() <= 20) {
        return;
    }
    const coast_render::Point& p = line[line.size() / 2];
    const double heading = std::atan2(-p.nx, p.nz) * 180.0 / 3.14159265358979323846;
    double light = 0.0, turn = 0.0;
    for (const auto& [d, lod] : std::array<std::pair<double, int>, 3>{{{64.0, 0}, {128.0, 1},
                                                                        {256.0, 2}}}) {
        const Eye eye{p.x + d * p.nx, p.z + d * p.nz, 0.5 * (p.foot + p.top), heading, 0.0, 50.0};
        const V3 sun = coast_image::sunOf(eye, 40.0, 30.0);
        const coast_bands::TileImage fine =
            coast_bands::renderTiles(tilesAround(p.x, p.z, lod, 60.0), eye, sun);
        const coast_bands::TileImage coarse =
            coast_bands::renderTiles(tilesAround(p.x, p.z, lod + 1, 60.0), eye, sun);
        const std::vector<double> a = fine.luminance(true);
        const std::vector<double> b = coarse.luminance(true);
        double diff = 0.0, mean = 0.0, n = 0.0;
        std::vector<double> turns;
        for (std::size_t at = 0; at < a.size(); ++at) {
            if (a[at] < 0.0 || b[at] < 0.0 || fine.rock[at] < 0.5 || coarse.rock[at] < 0.5 ||
                fine.height[at] < p.foot + 1.0 || fine.height[at] > p.top - 1.0) {
                continue;
            }
            diff += std::abs(a[at] - b[at]);
            mean += a[at];
            n += 1.0;
            const double c = std::clamp(coast_image::dot(fine.normal[at], coarse.normal[at]), -1.0,
                                        1.0);
            turns.push_back(std::acos(c) * 180.0 / 3.14159265358979323846);
        }
        std::sort(turns.begin(), turns.end());
        const double l = n > 0.0 ? diff / mean : 1.0;
        const double t = turns.empty() ? 90.0 : turns[turns.size() / 2];
        std::printf("LOD %d to %d at %.0f m: the light of the face %.3f of its mean apart, the "
                    "normals %.1f degrees apart in the median (%zu pixels)\n",
                    lod, lod + 1, d, l, t, turns.size());
        light = std::max(light, l);
        turn = std::max(turn, t);
    }
    TEST_CHECK(light <= kLodLight && turn <= kLodTurn);
}

// The views of the stacks (their eyes 200 m off, as coast_stack_test finds the stacks).
struct StackView {
    const char* name;
    double x, z;    // the stack
    Eye eye;
};
constexpr std::array<StackView, 4> kViews{{
    {"the needle", -15'901'339.0, -6'234'092.0, {-15'901'439.4, -6'233'918.6, 6.0, 30.0, 5.9, 50.0}},
    {"the pillar", -26'464'264.0, 7'231'922.0, {-26'464'363.8, 7'232'094.7, 6.0, 30.0, 6.0, 50.0}},
    {"the split stack", 23'228'361.0, -2'040'803.0, {23'228'261.2, -2'040'630.1, 6.0, 30.0, 3.6, 50.0}},
    {"the arch", -26'488'669.0, 7'401'453.0, {-26'488'769.4, 7'401'625.9, 6.0, 30.0, 6.0, 50.0}},
}};

// Its dark lines row by row and how much more its light changes up it than across it.
std::array<double, 2> layersOf(const coast_bands::TileImage& img, const std::vector<double>& l,
                               const StackView& v) {
    const auto stack = [&](std::size_t at) {
        const V3& q = img.point[at];
        return l[at] >= 0.0 && img.rock[at] >= 0.5 && q.y > 6.0 &&
               std::hypot(q.x - v.x, q.z - v.z) < 45.0;
    };
    std::vector<double> rows;
    for (int y = 0; y < kHeight; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < kWidth; ++x) {
            if (stack(static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x))) {
                first = first < 0 ? x : first;
                last = x;
            }
        }
        if (last - first < 8) {
            continue;
        }
        double sum = 0.0, n = 0.0;
        for (int x = first + (last - first) / 5; x <= last - (last - first) / 5; ++x) {
            const auto at = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
            if (stack(at)) {
                sum += l[at];
                n += 1.0;
            }
        }
        rows.push_back(n > 0.0 ? sum / n : 0.0);
    }
    double mean = 0.0;
    for (const double r : rows) {
        mean += r / static_cast<double>(std::max<std::size_t>(1, rows.size()));
    }
    int lines = 0;
    for (std::size_t i = 5; i + 5 < rows.size(); ++i) {
        double around = 0.0;
        for (std::size_t k = i - 5; k <= i + 5; ++k) {
            around += rows[k] / 11.0;
        }
        lines += rows[i] < rows[i - 1] && rows[i] <= rows[i + 1] &&
                         around - rows[i] >= kLineDepth * mean
                     ? 1
                     : 0;
    }
    double up = 0.0, across = 0.0;
    for (int y = 0; y + 1 < kHeight; ++y) {
        for (int x = 0; x + 1 < kWidth; ++x) {
            const auto at = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
            if (stack(at) && stack(at + 1) && stack(at + kWidth)) {
                up += std::abs(l[at + kWidth] - l[at]);
                across += std::abs(l[at + 1] - l[at]);
            }
        }
    }
    return {static_cast<double>(lines), across > 0.0 ? up / across : 0.0};
}

// How far the normals of its pixels turn from those of its faces around it, degrees (the mean of
// the angles of their horizontal parts, on its rock within kOnFace meters of its surface): the
// normals of the faces of the stack at the pixel (its spire alone, by differences kAcross meters
// apart across and a cell up, over the steps of its beds), those the tiles draw interpolated from
// their vertices (melted over a cell or two on either side of its edges, its edges soft).
double softnessOf(const coast_bands::TileImage& img, const StackView& v, double cell) {
    const terrain::RockColumn c = coast_planet::field().rockAt(v.x, v.z);
    if (!(c.spire.radius > 0.0f)) {
        return 180.0;
    }
    const double ax = v.x - static_cast<double>(c.spire.dx);
    const double az = v.z - static_cast<double>(c.spire.dz);
    const terrain::RockDetail detail{static_cast<float>(cell)};
    const auto density = [&](double x, double y, double z) {
        return static_cast<double>(terrain::spireDensity(c.spire, x - ax, z - az, y, detail));
    };
    double sum = 0.0;
    int n = 0;
    for (std::size_t at = 0; at < img.lit.size(); ++at) {
        const V3& q = img.point[at];
        if (img.lit[at] < 0.0 || q.y < kOnFoot || std::abs(density(q.x, q.y, q.z)) > kOnFace) {
            continue;
        }
        const double gx = density(q.x + kAcross, q.y, q.z) - density(q.x - kAcross, q.y, q.z);
        const double gz = density(q.x, q.y, q.z + kAcross) - density(q.x, q.y, q.z - kAcross);
        const V3& m = img.normal[at];
        if (std::hypot(gx, gz) < 1e-9 || std::hypot(m.x, m.z) < 0.3) {
            continue; // its top, its ledges
        }
        const double turn = std::atan2(gx * m.z - gz * m.x, gx * m.x + gz * m.z);
        sum += std::abs(turn) * 180.0 / std::numbers::pi;
        ++n;
    }
    return n > 0 ? sum / n : 180.0;
}

void testStacks() {
    for (const StackView& v : kViews) {
        const std::vector<terrain::TileData> tiles = tilesAround(v.x, v.z, 2, 70.0);
        const coast_bands::TileImage img =
            coast_bands::renderTiles(tiles, v.eye, coast_image::sunOf(v.eye, 60.0, kSunHigh));
        const std::array<double, 2> with = layersOf(img, img.luminance(true), v);
        const std::array<double, 2> without = layersOf(img, img.luminance(false), v);
        const double soft = softnessOf(img, v, terrain::tileCellMeters(2));
        std::printf("%s at 200 m (LOD 2), a sun %.0f degrees high: %.0f dark lines, its light "
                    "changing %.2f times more up it than across it (without the bands: %.0f, "
                    "%.2f); its normals %.1f degrees off its faces\n",
                    v.name, kSunHigh, with[0], with[1], without[0], without[1], soft);
        TEST_CHECK(with[0] >= kBands && with[1] >= kLayering && soft <= kSoft);
    }
}

} // namespace

int main() {
    testAlone();
    testPath();
    testLods();
    testStacks();
    return test::result();
}
