// The cells of the GPU horizon as its grid cuts them into triangles (agent 40, after the checks in
// Unreal of the orchestrator: vertical streaks down the cliff of the view 3 at 800 m, a zigzag of
// its normals one tooth per triangle in DebugView 7; small regular teeth at the foot of the wall
// of the fjord; saw teeth on the crests seen at about 1 km from falaise_cotiere). On the default
// world with its planet file, the grid of today (Fixed: every cell along the diagonal from
// (i + 1, k) to (i, k + 1)) and the candidate of agent 40 (Contour: the diagonal along the contour
// lines, horizon_split.hpp):
// - the comb of light of the wall of the view 3 (coast_far_test, testComb) in the image of agent 33
//   (coast_image_support.hpp: the normals of the grid bilinear over each cell, its cells cut along
//   their main diagonal) and in the image of the grid of the GPU (horizon_image_support.hpp: the
//   normals linear on the triangles), under a sun kSunHigh degrees high from either side;
// - the contour of the walls: along the faces of the notched cliff, of the calanque and of the
//   fjord (coast_render_support.hpp, lineOf), where the ground the grid shows crosses the height
//   of the middle of the face, its offset across the face: its part repeating at one to four
//   pitches of the level (periodicOf), meters;
// - the sky line of falaise_cotiere: from its eye, every column of an image kColumns wide over
//   kFov degrees, the highest angle of the ground the grid shows from kNear to the far end of the
//   level (level 0 to 1 km; level 1, its point heights or raised to its crests, to 2 km), and that
//   of the true ground (Heightfield::heightAt every quarter of a pitch of the level): the teeth of
//   the grid, the angle of the grid less that of the true ground less their mean difference over
//   kTeethWindow columns around, in pixels; and the teeth of the true ground itself (its angle
//   less its mean).
// Printed for the report of the orchestrator. The checks: enough wall and sky line, and Contour
// never worse than Fixed on the contour of the walls.
#include "horizon_image_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <vector>

namespace {

using water_horizon::CellSplit;
using water_horizon::DrawnLevel;
using water_horizon::SeaDrawing;

constexpr double kWall = 400.0;
constexpr double kSunHigh = 8.0;
constexpr std::array<double, 4> kSunAround{-90.0, -60.0, 60.0, 90.0};
constexpr int kLeastColumns = 600;
constexpr int kLeastPoints = 200;
constexpr double kWorseLeast = 1.05; // Contour never worse than Fixed beyond this share
constexpr int kColumns = 1920;
constexpr double kFov = 90.0;
constexpr int kShownColumns = 1200;
constexpr int kTeethWindow = 41;
constexpr double kNear = 400.0;

const char* nameOf(CellSplit split) {
    return split == CellSplit::Fixed ? "fixed" : "contour";
}

// The rectangle of a level holding a line, with a margin of six samples.
terrain::HorizonRect rectOf(int level, const std::vector<coast_render::Point>& line) {
    const double pitch = terrain::levelPitch(water_horizon::params(), level);
    double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
    for (const auto& p : line) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
    }
    const auto lo = [&](double v) { return static_cast<std::int64_t>(std::floor(v / pitch)) - 6; };
    return {lo(x0), lo(z0), lo(x1) + 13, lo(z1) + 13};
}

void testComb() {
    const std::vector<coast_render::Point> line = coast_render::lineOf(coast_rock::kCliff, kWall);
    const coast_render::Level0 agent33 = coast_render::level0Around(line);
    const DrawnLevel gpu = water_horizon::drawnLevel(0, rectOf(0, line));
    const coast_image::Eye eye{429'427.2, -855'233.8, 15.0, 29.1, 3.0, 50.0};
    for (const double azimuth : kSunAround) {
        const coast_image::V3 sun = coast_image::sunOf(eye, azimuth, kSunHigh);
        const coast_image::Comb a = coast_image::combOf(coast_image::render(agent33, eye, sun));
        std::printf("the wall of the view 3, a sun %.0f degrees high, %+.0f degrees from behind "
                    "the eye: its comb %.3f, %d strokes (%d columns) in the image of agent 33",
                    kSunHigh, azimuth, a.share, a.strokes, a.columns);
        for (const CellSplit split : {CellSplit::Fixed, CellSplit::Contour}) {
            const coast_image::Comb c =
                coast_image::combOf(horizon_image::render(gpu, eye, sun, split));
            std::printf("; %.3f, %d strokes (%d columns) on the triangles of the GPU, %s",
                        c.share, c.strokes, c.columns, nameOf(split));
            TEST_CHECK(c.columns >= kLeastColumns);
        }
        std::printf("\n");
    }
}

// Where the ground shown crosses the height h across the face at p, from 3 pitches behind its
// plane to 3 before it: the offset along its normal, or nothing.
bool crossing(const DrawnLevel& l, const coast_render::Point& p, double h, CellSplit split,
              double& offset) {
    const double reach = 3.0 * l.pitch;
    const double step = l.pitch / 16.0;
    const auto at = [&](double o) {
        return l.heightAt(p.x + o * p.nx, p.z + o * p.nz, split, SeaDrawing::InGround);
    };
    std::optional<double> before = at(-reach);
    for (double o = -reach + step; o <= reach && before; o += step) {
        const std::optional<double> here = at(o);
        if (!here) {
            return false;
        }
        if ((*before >= h) != (*here >= h)) {
            offset = o - step * (*here - h) / (*here - *before);
            return true;
        }
        before = here;
    }
    return false;
}

void testContour(const coast_rock::Place& place, int level) {
    const std::vector<coast_render::Point> line = coast_render::lineOf(place, kWall);
    const DrawnLevel l = water_horizon::drawnLevel(level, rectOf(level, line), false);
    double fixed = 0.0;
    for (const CellSplit split : {CellSplit::Fixed, CellSplit::Contour}) {
        std::vector<double> s, offsets;
        for (const auto& p : line) {
            double o = 0.0;
            if (p.top - p.foot > 2.0 * l.pitch &&
                crossing(l, p, 0.5 * (p.foot + p.top), split, o)) {
                s.push_back(p.s);
                offsets.push_back(o);
            }
        }
        const double teeth = coast_render::periodicOf(s, offsets, l.pitch, 4.0 * l.pitch);
        std::printf("%s, level %d (%.0f m), %s: the middle of its face %.2f m in and out at one "
                    "to four pitches (%zu points of %zu)\n",
                    place.name, level, l.pitch, nameOf(split), teeth, s.size(), line.size());
        TEST_CHECK(s.size() >= static_cast<std::size_t>(kLeastPoints));
        if (split == CellSplit::Fixed) {
            fixed = teeth;
        } else {
            TEST_CHECK(teeth <= kWorseLeast * fixed + 0.01);
        }
    }
}

// The sky line of a level from an eye: the highest angle of each column, radians, -10 where none.
std::vector<double> skyLine(const DrawnLevel& l, double x, double z, double eye, double heading,
                            double far, CellSplit split) {
    std::vector<double> out(kShownColumns, -1.0);
    const double pixel = kFov / kColumns;
    for (int c = 0; c < kShownColumns; ++c) {
        const double az = (heading + (c - kShownColumns / 2 + 0.5) * pixel) * std::numbers::pi /
                          180.0;
        std::optional<double> best;
        for (double d = kNear; d <= far; d += 1.0) {
            const std::optional<double> h =
                l.heightAt(x + d * std::sin(az), z - d * std::cos(az), split, SeaDrawing::InGround);
            if (h) {
                best = std::max(best.value_or(-10.0), std::atan2(*h - eye, d));
            }
        }
        out[static_cast<std::size_t>(c)] = best.value_or(-10.0);
    }
    return out;
}

// The sky line of the true ground from an eye (Heightfield::heightAt every step meters).
std::vector<double> trueSkyLine(double x, double z, double eye, double heading, double far,
                                double step) {
    std::vector<double> out(kShownColumns, -10.0);
    const double pixel = kFov / kColumns;
    std::vector<double> xs, zs, ds;
    for (double d = kNear; d <= far; d += step) {
        ds.push_back(d);
    }
    xs.resize(ds.size());
    zs.resize(ds.size());
    std::vector<float> h(ds.size());
    for (int c = 0; c < kShownColumns; ++c) {
        const double az = (heading + (c - kShownColumns / 2 + 0.5) * pixel) * std::numbers::pi /
                          180.0;
        for (std::size_t n = 0; n < ds.size(); ++n) {
            xs[n] = x + ds[n] * std::sin(az);
            zs[n] = z - ds[n] * std::cos(az);
        }
        coast_planet::field().heightsAt(xs, zs, h);
        for (std::size_t n = 0; n < ds.size(); ++n) {
            out[static_cast<std::size_t>(c)] = std::max(out[static_cast<std::size_t>(c)],
                                                         std::atan2(h[n] - eye, ds[n]));
        }
    }
    return out;
}

// The teeth of a sky line against another (none: against 0): their difference less its mean over
// kTeethWindow columns, in pixels (root of the mean square, and the largest).
std::array<double, 2> teethOf(const std::vector<double>& line, const std::vector<double>& base) {
    const double pixel = kFov / kColumns * std::numbers::pi / 180.0;
    const auto at = [&](int c) {
        const auto i = static_cast<std::size_t>(c);
        return line[i] - (base.empty() ? 0.0 : base[i]);
    };
    double square = 0.0, most = 0.0;
    int n = 0;
    for (int c = kTeethWindow / 2; c + kTeethWindow / 2 < kShownColumns; ++c) {
        double sum = 0.0;
        for (int u = c - kTeethWindow / 2; u <= c + kTeethWindow / 2; ++u) {
            sum += at(u);
        }
        const double part = (at(c) - sum / kTeethWindow) / pixel;
        square += part * part;
        most = std::max(most, std::abs(part));
        ++n;
    }
    return {std::sqrt(square / std::max(n, 1)), most};
}

void testSkyLine() {
    constexpr double x = 4'944'509.0, z = -8'706'669.0, eye = 2.0, heading = 225.0;
    const double rad = heading * std::numbers::pi / 180.0;
    for (int level = 0; level < 2; ++level) {
        const double pitch = terrain::levelPitch(water_horizon::params(), level);
        const double far = 125.0 * pitch;
        const double cx = x + 0.5 * far * std::sin(rad), cz = z - 0.5 * far * std::cos(rad);
        const auto half = static_cast<std::int64_t>(0.6 * far / pitch);
        const std::vector<double> truth = trueSkyLine(x, z, eye, heading, far, pitch / 4.0);
        const auto [trueRms, trueMost] = teethOf(truth, {});
        std::printf("the sky line of falaise_cotiere, the true ground, %.0f to %.0f m: its own "
                    "teeth %.2f px (root of the mean square), %.2f px at most\n",
                    kNear, far, trueRms, trueMost);
        for (const bool raise : {false, true}) {
            if (level == 0 && raise) {
                continue; // level 0 never rises
            }
            const DrawnLevel l = water_horizon::drawnLevel(
                level, water_horizon::rectAround(level, cx, cz, half), false, raise);
            for (const CellSplit split : {CellSplit::Fixed, CellSplit::Contour}) {
                const std::vector<double> line = skyLine(l, x, z, eye, heading, far, split);
                const auto [rms, most] = teethOf(line, truth);
                const auto shown = static_cast<int>(
                    std::count_if(line.begin(), line.end(), [](double a) { return a > -9.0; }));
                std::printf("the sky line of falaise_cotiere, level %d (%.0f m, %.0f to %.0f m), "
                            "%s, %s: teeth of %.2f px against the true ground (root of the mean "
                            "square), %.2f px at most, over %d columns\n",
                            level, pitch, kNear, far, raise ? "raised to its crests" : "points",
                            nameOf(split), rms, most, shown);
                TEST_CHECK(shown == kShownColumns);
            }
        }
    }
}

} // namespace

int main() {
    testComb();
    testContour(coast_rock::kCliff, 0);
    testContour(coast_rock::kCalanque, 0);
    testContour(coast_rock::kFjord, 0);
    testContour(coast_rock::kFjord, 1);
    testSkyLine();
    return test::result();
}
