// The coasts seen from afar on the horizon (agent 33, after the checks in Unreal of d857a1b and
// e6f198d: bands and strokes of light along the cliff of the view 3, 800 m out to sea; white teeth
// at the water by the left wall of the view fjord_axe), on the default world with its planet file:
// - The wall of the notched cliff as the view 3 shows it (kWall meters on either side of its
//   place: coast_render_support.hpp, lineOf): on lines along it from kAcrossHigh meters behind
//   the plane of its face to as far before it (the ramp of the horizon from the top to the foot),
//   the light of a low sun (behind the eye, and from the side) by the detail normals of the level 0
//   and by its samples, its part repeating with a wavelength of one to four steps of their lattice
//   kLightPeriodic at most (no row of triangles anywhere on the ramp). Its image as the GPU horizon
//   draws it from the eye of the view 3 (its level 0 lit by its detail normals, an image of 1200
//   columns 50 degrees wide; the check in Unreal of e6f198d: a comb of light strokes down the
//   wall), under a sun kSunHigh degrees high from either side or from behind (kSunAround): the
//   light of each column of the wall (over its middle) less its mean over 41 columns around (about
//   25 m), kComb of the mean light at most (root of the mean square), and kStrokes narrow strokes
//   of light at most (peaks above 6 % of the mean, at most 10 columns wide); 0.164 and 11 on
//   d857a1b, 0.137 and 15 on e6f198d, whose walls turned at blocks of 40 m within a few meters.
// - The left wall of the fjord of the view fjord_axe (a bank without a face): the sea the
//   material of the horizon paints up it between a sea sample and the land sample next to it (the
//   ground interpolated between them crosses the sea level there) kPainted meters at most on the
//   levels 0 and 1 (the shore of the horizon rising from the sea level: coast_faces::seenShore;
//   1.5 and 3 m on d857a1b). The rest is the mask of the sea of that material (agent 40).
#include "coast_image_support.hpp"
#include "coast_render_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace coast_render;

constexpr double kWall = 400.0;
constexpr double kAcrossHigh = 14.0;
constexpr double kAcrossStep = 2.0;
constexpr double kLightPeriodic = 0.03;
constexpr double kSunHigh = 8.0;
constexpr std::array<double, 4> kSunAround{-90.0, -60.0, 60.0, 90.0};
constexpr double kComb = 0.075;
constexpr int kStrokes = 3;
constexpr int kLeastColumns = 900;
constexpr std::array<double, 2> kPainted{1.0, 2.0};
// The left wall of the view fjord_axe (1 819 677 ; 9 928 285, heading 70): its shore in this box.
constexpr std::array<double, 4> kFjordWall{1'819'400.0, 9'926'400.0, 1'820'060.0, 9'927'800.0};

// The suns: behind the eye of the view 3 a little to its left and high, and low from its side.
std::array<std::array<double, 3>, 2> suns() {
    const double fx = 0.486, fz = -0.874; // heading 29.1
    const double rx = 0.874, rz = 0.486;
    std::array<std::array<double, 3>, 2> out{{{-fx - 0.35 * rx, 0.8, -fz - 0.35 * rz},
                                              {-0.5 * fx + rx, 0.5, -0.5 * fz + rz}}};
    for (auto& s : out) {
        const double l = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        s = {s[0] / l, s[1] / l, s[2] / l};
    }
    return out;
}

void testRamp(const std::vector<Point>& line, const Level0& l) {
    double most = 0.0;
    for (double off = -kAcrossHigh; off <= kAcrossHigh; off += kAcrossStep) {
        for (int fine = 0; fine < 2; ++fine) {
            const double lattice = fine == 0 ? kPitch / kRatio : kPitch;
            for (const auto& sun : suns()) {
                std::vector<double> s, lit;
                for (const Point& p : line) {
                    const auto [gx, gz] = l.slopeAt(p.x + off * p.nx, p.z + off * p.nz, fine == 0);
                    s.push_back(p.s);
                    lit.push_back(std::max(0.0, (-gx * sun[0] + sun[1] - gz * sun[2]) /
                                                    std::sqrt(gx * gx + 1.0 + gz * gz)));
                }
                most = std::max(most, periodicOf(s, lit, lattice, 4.0 * lattice));
            }
        }
    }
    std::printf("the wall of the view 3, from %.0f m behind its face to as far before it: its "
                "light by the normals of the horizon %.3f repeating at one to four steps of their "
                "lattice at most\n",
                kAcrossHigh, most);
    TEST_CHECK(most <= kLightPeriodic);
}

// The image of the wall (coast_image_support.hpp): from the eye of the view 3, under a sun kSunHigh
// degrees high from either side or from behind.
void testComb(const Level0& l) {
    const coast_image::Eye eye{429'427.2, -855'233.8, 15.0, 29.1, 3.0, 50.0};
    double most = 0.0;
    int strokes = 0;
    for (const double azimuth : kSunAround) {
        const coast_image::Comb c = coast_image::combOf(
            coast_image::render(l, eye, coast_image::sunOf(eye, azimuth, kSunHigh)));
        std::printf("its image under a sun %.0f degrees high, %+.0f degrees from behind the eye: "
                    "its comb %.3f of its light, %d narrow strokes over %d columns\n",
                    kSunHigh, azimuth, c.share, c.strokes, c.columns);
        TEST_CHECK(c.columns >= kLeastColumns);
        most = std::max(most, c.share);
        strokes = std::max(strokes, c.strokes);
    }
    TEST_CHECK(most <= kComb && strokes <= kStrokes);
}

void testFjordWall() {
    static const terrain::Ecology ecology(terrain::kDefaultWorldSeed);
    static const terrain::HorizonParams params{};
    static const terrain::HorizonSampler sampler(field(), ecology, params);
    for (int level = 0; level < 2; ++level) {
        const int pitch = terrain::levelPitch(params, level);
        const auto at = [&](double v) { return static_cast<std::int64_t>(std::floor(v / pitch)); };
        const HorizonRect r{at(kFjordWall[0]), at(kFjordWall[1]), at(kFjordWall[2]) + 1,
                            at(kFjordWall[3]) + 1};
        const std::vector<terrain::HorizonCell> cells = sampler.sample(level, r);
        const std::int64_t w = r.width();
        const auto cell = [&](std::int64_t i, std::int64_t k) -> const terrain::HorizonCell& {
            return cells[static_cast<std::size_t>(k * w + i)];
        };
        const auto sea = [](const terrain::HorizonCell& c) {
            return (c.flags & terrain::kHorizonSea) != 0 && c.height < 0.0f;
        };
        double painted = 0.0;
        for (std::int64_t k = 1; k + 1 < r.height(); ++k) {
            for (std::int64_t i = 1; i + 1 < w; ++i) {
                if (!sea(cell(i, k))) {
                    continue;
                }
                for (const auto& [di, dk] : std::array<std::array<int, 2>, 4>{
                         {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                    const terrain::HorizonCell& n = cell(i + di, k + dk);
                    const double h = n.height;
                    const double g = cell(i, k).height;
                    painted = !sea(n) && h > 0.0 ? std::max(painted, h * -g / (h - g)) : painted;
                }
            }
        }
        std::printf("the left wall of the fjord of the view fjord_axe, level %d (%d m): the sea "
                    "painted up it %.2f m at most\n",
                    level, pitch, painted);
        TEST_CHECK(painted <= kPainted[static_cast<std::size_t>(level)]);
    }
}

} // namespace

int main() {
    const std::vector<Point> line = lineOf(coast_rock::kCliff, kWall);
    std::printf("the notched cliff: its line traced over %.0f m\n",
                line.empty() ? 0.0 : line.back().s);
    TEST_CHECK(static_cast<double>(line.size()) * kStep >= kWall);
    if (line.size() >= 8) {
        const Level0 l = level0Around(line);
        testRamp(line, l);
        testComb(l);
    }
    testFjordWall();
    return test::result();
}
