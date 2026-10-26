// Views of the relief in perspective, on synthetic grounds: the projection puts a wall where its
// top is seen, a nearer wall hides a farther one, the tower stands where it is placed, the image
// does not depend on the number of threads, the edges of a steep face fall where the ground turns
// (not on the steps of the rays), and the sight lines see over the ground or not.
#include "terrain/io/relief_view.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <stdexcept>

namespace {

using terrain::Image;
using terrain::ViewParams;

constexpr double kPi = 3.14159265358979323846;

// Flat ground at 10 m, and a wall of height h from x = 5,000 m on (east of the eye).
terrain::ViewGround wall(float h) {
    return [h](double x, double /*z*/) { return x >= 5000.0 ? h : 10.0f; };
}

// The eye 2 m above the flat ground at the origin, looking east, without curvature or tower.
ViewParams eastView() {
    ViewParams p;
    p.eyeHeight = 12.0;
    p.heading = 0.25;
    p.width = 320;
    p.height = 180;
    p.planetRadius = 1.0e30;
    p.maxDistance = 20'000.0;
    return p;
}

bool isSky(const Image& image, int column, int row) {
    const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(column)) *
                    3;
    // The sky goes from (70, 120, 200) to the haze (200, 215, 235): always bluer than red.
    return image.pixels[at + 2] > image.pixels[at] + 30 &&
           image.pixels[at + 2] > image.pixels[at + 1] + 15;
}

bool isTowerRed(const Image& image, int column, int row) {
    const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(column)) *
                    3;
    return image.pixels[at] > 150 && image.pixels[at + 1] < 120 && image.pixels[at + 2] < 120;
}

// Focal length in pixels of eastView: the half width over tan(35 degrees).
double focal(const ViewParams& p) { return 0.5 * p.width / std::tan(kPi * p.fieldOfView); }

// The top of the wall is seen at tan = (1,000 - 12) / 5,000 above the axis: sky above that row,
// ground below, in the middle column; the flat ground in the lowest row.
void testWallProjection() {
    const ViewParams p = eastView();
    const Image image = terrain::renderReliefView(wall(1000.0f), p, 1);
    TEST_CHECK(image.width == 320 && image.height == 180 && image.channels == 3);
    const int top = static_cast<int>(std::floor(0.5 * p.height - focal(p) * 988.0 / 5000.0));
    TEST_CHECK(top > 5 && top < 85);
    TEST_CHECK(isSky(image, 160, top - 2));
    TEST_CHECK(!isSky(image, 160, top + 2));
    TEST_CHECK(!isSky(image, 160, p.height - 1));
    TEST_CHECK(isSky(image, 160, 0));
}

// A nearer wall higher on the image hides the farther one: its top is the skyline.
void testOcclusion() {
    ViewParams p = eastView();
    const terrain::ViewGround twoWalls = [](double x, double /*z*/) {
        if (x >= 5000.0) {
            return 1000.0f;
        }
        return x >= 2000.0 ? 600.0f : 10.0f;
    };
    const Image image = terrain::renderReliefView(twoWalls, p, 1);
    const int nearTop = static_cast<int>(std::floor(0.5 * p.height - focal(p) * 588.0 / 2000.0));
    TEST_CHECK(isSky(image, 160, nearTop - 2));
    TEST_CHECK(!isSky(image, 160, nearTop + 2));
    // Tilted up, the same skyline moves down by the tilt.
    p.tilt = 0.1;
    const Image tilted = terrain::renderReliefView(twoWalls, p, 1);
    const int shifted = nearTop + static_cast<int>(std::lround(focal(p) * 0.1));
    TEST_CHECK(isSky(tilted, 160, shifted - 2));
    TEST_CHECK(!isSky(tilted, 160, shifted + 2));
}

// A tower of 330 m 2 km ahead stands in the middle columns, red at its foot band.
void testTower() {
    ViewParams p = eastView();
    p.towerX = 2000.0;
    p.towerZ = 0.0;
    p.towerHeight = 330.0;
    p.towerWidth = 40.0;
    const terrain::ViewGround flat = [](double, double) { return 10.0f; };
    const Image image = terrain::renderReliefView(flat, p, 1);
    // Its foot is 2 m below the eye, 2 km away: just below the axis; its first band is red.
    const int foot = static_cast<int>(std::floor(0.5 * p.height + focal(p) * 2.0 / 2000.0));
    TEST_CHECK(isTowerRed(image, 160, foot - 2));
    // Its top: 318 m above the eye.
    const int top = static_cast<int>(std::floor(0.5 * p.height - focal(p) * 318.0 / 2000.0));
    TEST_CHECK(isSky(image, 160, top - 2));
    TEST_CHECK(!isSky(image, 160, top + 2));
    // 20 m on each side at 2 km: about 2 columns; none far from the middle.
    TEST_CHECK(!isTowerRed(image, 140, foot - 2) && !isTowerRed(image, 180, foot - 2));
}

// The pixels of rows from[column] + 1 to + 4 that differ from those of the column on their left,
// from column 101 to 539: none along a smooth edge.
int teethAlong(const Image& image, const std::function<int(int)>& from) {
    const auto pixel = [&](int column, int row, int channel) {
        return static_cast<int>(
            image.pixels[(static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                          static_cast<std::size_t>(column)) *
                             3 +
                         static_cast<std::size_t>(channel)]);
    };
    int teeth = 0;
    for (int column = 101; column < 540; ++column) {
        for (int k = 1; k <= 4; ++k) {
            for (int channel = 0; channel < 3; ++channel) {
                if (std::abs(pixel(column, from(column) + k, channel) -
                             pixel(column - 1, from(column - 1) + k, channel)) > 3) {
                    ++teeth;
                    break;
                }
            }
        }
    }
    return teeth;
}

// A face of 6 m per meter across the view, oblique to it, from 10 m to a plateau of 22 m 45 m
// away: its top is the skyline and the rows just below it change smoothly from one column to the
// next; so do the lowest rows of the image when its foot lies below them. On the steps of the
// rays alone, the last of them on the face fell anywhere below its top (the first one seen
// anywhere above its foot), and the rows between took another color in some columns: regular
// teeth along the edges.
void testFaceEdges() {
    ViewParams p = eastView();
    p.width = 640;
    p.height = 360;
    const terrain::ViewGround face = [](double x, double z) {
        const double u = 0.955 * x + 0.296 * z; // across the face, 0.3 radian off the axis
        return static_cast<float>(std::clamp(10.0 + 6.0 * (u - 43.0), 10.0, 22.0));
    };
    const Image image = terrain::renderReliefView(face, p, 1);
    const auto skyline = [&](int column) {
        int top = 0;
        while (top < p.height && isSky(image, column, top)) {
            ++top;
        }
        return top;
    };
    for (int column = 101; column < 540; ++column) {
        TEST_CHECK(skyline(column) > 40 && skyline(column) < 140);
        TEST_CHECK(std::abs(skyline(column) - skyline(column - 1)) <= 1);
    }
    TEST_CHECK(teethAlong(image, skyline) == 0);
    // Tilted up by 0.4 radian: the lowest row looks 1.5 degrees up, above the foot of the face.
    p.tilt = 0.4;
    const Image raised = terrain::renderReliefView(face, p, 1);
    TEST_CHECK(!isSky(raised, 320, p.height - 1));
    TEST_CHECK(teethAlong(raised, [&](int) { return p.height - 5; }) == 0);
}

void testThreadsAndCurvature() {
    ViewParams p = eastView();
    p.planetRadius = 6'371'000.0;
    p.maxDistance = 200'000.0;
    const terrain::ViewGround hills = [](double x, double z) {
        return static_cast<float>(300.0 * std::sin(x / 7000.0) * std::cos(z / 5000.0) + 200.0);
    };
    const Image one = terrain::renderReliefView(hills, p, 1);
    const Image four = terrain::renderReliefView(hills, p, 4);
    TEST_CHECK(one.pixels == four.pixels);
}

void testSightline() {
    const ViewParams p = eastView();
    // Over flat ground, a point 1,000 m high 4 km away is seen at tan 0.247.
    const terrain::ViewGround flat = [](double, double) { return 10.0f; };
    const terrain::Sightline seen = terrain::sightline(flat, p, 4000.0, 0.0, 1000.0, 100.0);
    TEST_CHECK(seen.visible);
    TEST_CHECK(std::abs(seen.distance - 4000.0) < 1e-9);
    TEST_CHECK(std::abs(seen.tangent - 988.0 / 4000.0) < 1e-6);
    // A wall of 1,000 m from 5 km on hides a point of 900 m at 8 km.
    const terrain::Sightline hidden = terrain::sightline(wall(1000.0f), p, 8000.0, 0.0, 900.0, 100.0);
    TEST_CHECK(!hidden.visible);
    TEST_CHECK(hidden.blockedAt >= 5000.0 && hidden.blockedAt < 5100.0);
    // Its own summit (within the margin) does not hide it.
    const terrain::Sightline own = terrain::sightline(wall(1000.0f), p, 5050.0, 0.0, 1000.0, 100.0);
    TEST_CHECK(own.visible);
}

void testInvalid() {
    const terrain::ViewGround flat = [](double, double) { return 0.0f; };
    int thrown = 0;
    for (int k = 0; k < 4; ++k) {
        ViewParams p = eastView();
        if (k == 0) {
            p.width = 1;
        } else if (k == 1) {
            p.fieldOfView = 0.5;
        } else if (k == 2) {
            p.hazeDistance = 0.0;
        } else {
            p.maxDistance = 1.0;
        }
        try {
            (void)terrain::renderReliefView(flat, p, 1);
        } catch (const std::invalid_argument&) {
            ++thrown;
        }
    }
    TEST_CHECK(thrown == 4);
}

} // namespace

int main() {
    testWallProjection();
    testOcclusion();
    testTower();
    testFaceEdges();
    testThreadsAndCurvature();
    testSightline();
    testInvalid();
    return test::result();
}
