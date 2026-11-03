// The top of the walls of the coasts as Unreal draws them (agent 33), on the default world with its
// planet file: nothing along it at a regular step (the check in Unreal of 06fd02f: a saw along the
// top of the wall of the calanque of the view 5 seen from 300 m, the wall gridded with fine
// vertical lines).
// - The vertices of the tiles on a face (their normal within kSteep of the horizontal, from kEnds
//   meters over its foot to kEnds under its top, within kNear meters of its plane) take all of its
//   rock (rockShare kBare at least): each took the share of its nearest column, none where that
//   column stood more than 3 m off the face, at one vertex in a few down the faces of LOD 2 and 3
//   (stripes of the ground every few cells, a tooth of them at the top).
// - Along the line of the face (the wall of the calanque from the eye of the view 5, the notched
//   cliff from kAway meters out at sea), on the tiles of the rings of Unreal around the eye: the
//   light of the face kBelow meters under its top on their image (lit by a sun kSunHigh high from
//   kSunAround, its rock under its bands, the ground of another albedo kGround), and the sky line
//   over it, exactly on their triangles. Each less its mean over kTrend meters: its
//   autocorrelation at 2, 4 and 8 m and at the steps where the face crosses the cells of 8 m,
//   reported; its part at the lattice of the cells of its tiles (2, 4 or 8 m: the signal folded on
//   the phase of each sample in its cell, the root of the mean square of the means of its phases)
//   kLightMost at most, and for the sky line kSkyPixels of a pixel of Unreal at most (a screen of
//   kScreen pixels over 90 degrees, at the distance of the face, from peak to peak: a saw).
// - The notched cliff seen from 800 m (the level 0 of the horizon, the view 3), over kLongHalf
//   meters both ways: the light under its top and its sky line on the image, less their mean over
//   twice the step of the blocks of its wall (coast_shape::kBlockCell, 150 m), their
//   autocorrelation at that step kPeriodic at most.
#include "coast_band_support.hpp"
#include "coast_image_support.hpp"
#include "coast_planet_support.hpp"
#include "coast_render_support.hpp"
#include "coast_rock_support.hpp"

#include "terrain/streaming/lod.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace {

using coast_image::Eye;
using coast_image::V3;
using coast_image::kHeight;
using coast_image::kWidth;
using coast_render::Point;

constexpr double kSteep = 0.3;
constexpr double kEnds = 3.0;
constexpr double kNear = 2.0;
constexpr double kBare = 0.99;
constexpr double kAway = 300.0;
constexpr double kEye = 15.0;
constexpr double kSunHigh = 30.0;
constexpr double kSunAround = 50.0;
constexpr double kBelow = 3.0;
// A point of the face is seen where the pixel it falls on shows the rock within kSeen meters of it
// (its top rounded back from the plane of its face).
constexpr double kSeen = 15.0;
constexpr double kGround = 0.6;
constexpr double kTrend = 32.0;
constexpr double kLightMost = 0.02;
constexpr double kSkyPixels = 0.75;
constexpr double kScreen = 2560.0;
constexpr double kPeriodic = 0.5;
constexpr double kBlockStep = 150.0;
constexpr double kHalf = 150.0;
constexpr double kLongHalf = 700.0;
constexpr int kBins = 4;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The eye kAway meters out from a point of a face, kEye meters over the sea, looking at it.
Eye eyeBefore(const Point& p) {
    const double heading = std::atan2(-p.nx, p.nz) * 180.0 / std::numbers::pi;
    return {p.x + kAway * p.nx, p.z + kAway * p.nz, kEye, heading, 3.0, 50.0};
}

// The tiles of the rings of Unreal around the eye (TerrainWorld.h) over the line, and their LOD.
struct Tiles {
    std::vector<terrain::TileData> data;
    std::vector<int> lods;
};

Tiles tilesOver(const Eye& e, const std::vector<Point>& line) {
    double x0 = 1e300, x1 = -1e300, z0 = 1e300, z1 = -1e300;
    for (const Point& p : line) {
        x0 = std::min(x0, p.x - 16.0);
        x1 = std::max(x1, p.x + 16.0);
        z0 = std::min(z0, p.z - 16.0);
        z1 = std::max(z1, p.z + 16.0);
    }
    terrain::LodParams lod;
    lod.viewDistance = 32;
    const terrain::ChunkCoord player{
        static_cast<std::int32_t>(std::floor(e.x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(e.z / terrain::kChunkSizeMeters))};
    Tiles tiles;
    for (const terrain::TileRequest& r : terrain::tileRequests(terrain::selectTiles(player, lod))) {
        const double size = terrain::tileSizeMeters(r.key.lod);
        const double ox = r.key.origin.x * terrain::kChunkSizeMeters;
        const double oz = r.key.origin.z * terrain::kChunkSizeMeters;
        if (ox <= x1 && ox + size >= x0 && oz <= z1 && oz + size >= z0) {
            tiles.data.push_back(coast_planet::tiles().generate(r));
            tiles.lods.push_back(r.key.lod);
        }
    }
    return tiles;
}

// The share of the vertices of the tiles on the face of the line that take less than kBare of
// its rock, and how many lie there.
double strippedShare(const std::vector<terrain::TileData>& tiles, const std::vector<Point>& line,
                     int& count) {
    int stripped = 0;
    count = 0;
    for (const terrain::TileData& t : tiles) {
        for (std::size_t v = 0; v < t.mesh.vertices.size() && !t.rock.empty(); ++v) {
            if (std::abs(t.mesh.normals[v].y) > kSteep) {
                continue;
            }
            const double x = t.originX + t.mesh.vertices[v].x;
            const double y = t.originY + t.mesh.vertices[v].y;
            const double z = t.originZ + t.mesh.vertices[v].z;
            const auto nearest =
                std::min_element(line.begin(), line.end(), [&](const Point& a, const Point& b) {
                    return std::hypot(a.x - x, a.z - z) < std::hypot(b.x - x, b.z - z);
                });
            const Point& p = *nearest;
            const double across = (x - p.x) * p.nx + (z - p.z) * p.nz - coast_render::kInland;
            if (std::hypot(p.x - x, p.z - z) > 4.0 || std::abs(across) > kNear ||
                y < p.foot + kEnds || y > p.top - kEnds) {
                continue;
            }
            ++count;
            stripped += t.rock[v] < kBare ? 1 : 0;
        }
    }
    return count > 0 ? static_cast<double>(stripped) / count : 0.0;
}

// The light of the face kBelow under its top at each point of the line, on an image (its light
// per pixel, below 0 for the sky) with the world point of each pixel (none: the horizon, seen
// whole); and the sky line over it there, in meters at the point, on the image.
struct Along {
    std::vector<double> light;
    std::vector<double> sky;
};

Along alongTop(const std::vector<double>& light, const std::vector<V3>* points, const Eye& e,
               const std::vector<Point>& line) {
    const coast_image::Frame fr = coast_image::frameOf(e);
    Along out;
    for (const Point& p : line) {
        const double off = coast_render::kInland + 0.5;
        const V3 at{p.x + off * p.nx, p.top - kBelow, p.z + off * p.nz};
        const V3 d = coast_image::sub(at, fr.eye);
        const double zc = coast_image::dot(d, fr.f);
        const int px = static_cast<int>(kWidth / 2.0 + coast_image::dot(d, fr.r) / zc * fr.k);
        int py = static_cast<int>(kHeight / 2.0 - coast_image::dot(d, fr.u) / zc * fr.k);
        const auto index = [&](int row) {
            return static_cast<std::size_t>(row) * kWidth + static_cast<std::size_t>(px);
        };
        bool seen = zc > 1.0 && px >= 0 && px < kWidth && py > 0 && py < kHeight &&
                    light[index(py)] >= 0.0;
        if (seen && points != nullptr) {
            const V3 w = (*points)[index(py)];
            seen = std::hypot(w.x - at.x, w.y - at.y, w.z - at.z) < kSeen;
        }
        out.light.push_back(seen ? light[index(py)] : kNaN);
        const int row = py;
        while (seen && py > 0 && light[index(py - 1)] >= 0.0) {
            --py;
        }
        out.sky.push_back(seen ? (row - py) * zc / fr.k : kNaN);
    }
    return out;
}

// The highest the triangles of the tiles rise seen from the eye in the vertical plane through
// (dx, dz) from it (far meters away), up to kBehind meters beyond: as a slope from the eye.
constexpr double kBehind = 50.0;

double highestSlope(const std::vector<terrain::TileData>& tiles, const Eye& e, double dx,
                    double dz, double far) {
    double highest = -1e300;
    for (const terrain::TileData& t : tiles) {
        const auto at = [&](std::uint32_t i) {
            return V3{t.originX + t.mesh.vertices[i].x, t.originY + t.mesh.vertices[i].y,
                      t.originZ + t.mesh.vertices[i].z};
        };
        for (std::size_t q = 0; q + 2 < t.mesh.indices.size(); q += 3) {
            const std::array<V3, 3> v{at(t.mesh.indices[q]), at(t.mesh.indices[q + 1]),
                                      at(t.mesh.indices[q + 2])};
            std::array<double, 3> side{};
            for (std::size_t k = 0; k < 3; ++k) {
                side[k] = (v[k].x - e.x) * -dz + (v[k].z - e.z) * dx;
            }
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t n = (k + 1) % 3;
                if ((side[k] < 0.0) == (side[n] < 0.0)) {
                    continue;
                }
                const double u = side[k] / (side[k] - side[n]);
                const double x = v[k].x + u * (v[n].x - v[k].x);
                const double z = v[k].z + u * (v[n].z - v[k].z);
                const double ahead = ((x - e.x) * dx + (z - e.z) * dz) / far;
                if (ahead > 1.0 && ahead < far + kBehind) {
                    const double y = v[k].y + u * (v[n].y - v[k].y);
                    highest = std::max(highest, (y - e.height) / ahead);
                }
            }
        }
    }
    return highest;
}

// The sky line over each point of the line seen from the eye, exactly on the triangles of the
// tiles (finer than the pixels: a step of a tenth of a pixel shows), as a height at the point
// over its top.
std::vector<double> skyLine(const std::vector<terrain::TileData>& tiles, const Eye& e,
                            const std::vector<Point>& line) {
    std::vector<double> out;
    for (const Point& p : line) {
        const double far = std::hypot(p.x - e.x, p.z - e.z);
        const double slope = highestSlope(tiles, e, p.x - e.x, p.z - e.z, far);
        out.push_back(slope > -1e299 ? e.height + slope * far - p.top : kNaN);
    }
    return out;
}

// v less its mean over trend meters (samples every kStep along the line; NaN where unseen).
std::vector<double> detrended(const std::vector<double>& v, double trend) {
    const auto half = static_cast<int>(trend / 2.0 / coast_render::kStep);
    const auto size = static_cast<int>(v.size());
    std::vector<double> out(v.size(), kNaN);
    for (int i = 0; i < size; ++i) {
        double sum = 0.0;
        int n = 0;
        for (int j = std::max(0, i - half); j <= std::min(size - 1, i + half); ++j) {
            if (!std::isnan(v[static_cast<std::size_t>(j)])) {
                sum += v[static_cast<std::size_t>(j)];
                ++n;
            }
        }
        if (n > half && !std::isnan(v[static_cast<std::size_t>(i)])) {
            out[static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(i)] - sum / n;
        }
    }
    return out;
}

// The autocorrelation of h at lag meters, and the root of its mean square.
double autocorrelation(const std::vector<double>& h, double lag, double& rms) {
    const auto l = static_cast<std::size_t>(std::lround(lag / coast_render::kStep));
    double power = 0.0, product = 0.0;
    int n = 0, pairs = 0;
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (std::isnan(h[i])) {
            continue;
        }
        power += h[i] * h[i];
        ++n;
        if (i + l < h.size() && !std::isnan(h[i + l])) {
            product += h[i] * h[i + l];
            ++pairs;
        }
    }
    rms = n > 0 ? std::sqrt(power / n) : 0.0;
    return pairs > n / 2 && power > 0.0 ? (product / pairs) / (power / n) : 0.0;
}

// The part of h at the lattice of the cells of its tiles: h folded on the phase of each sample in
// its cell along x and z (kBins x kBins), the root of the mean square of the means of the phases.
// A pattern of the cells (a step, a stripe at each cell the face crosses) comes back at the same
// phase; anything else averages out, to its mean square times the number of phases over the
// number of samples, taken out (a wall of uneven top shows no part of it at the lattice). Along an
// axis the face crosses the cells of more slowly than every kTrend meters, no phase (it would fold
// the shape of the face, longer than kTrend, on a few runs of it).
double latticePart(const std::vector<Point>& line, const std::vector<double>& h,
                   const std::vector<double>& cells) {
    std::vector<double> sum(kBins * kBins, 0.0);
    std::vector<int> count(kBins * kBins, 0);
    int n = 0;
    double all = 0.0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (std::isnan(h[i]) || !(cells[i] > 0.0)) {
            continue;
        }
        const auto phase = [&](double v) {
            const double f = v / cells[i] - std::floor(v / cells[i]);
            return std::min(kBins - 1, static_cast<int>(f * kBins));
        };
        const double x = line[i].x + coast_render::kInland * line[i].nx;
        const double z = line[i].z + coast_render::kInland * line[i].nz;
        const bool acrossX = cells[i] <= kTrend * std::abs(line[i].nz);
        const bool acrossZ = cells[i] <= kTrend * std::abs(line[i].nx);
        const auto bin = static_cast<std::size_t>((acrossX ? phase(x) : 0) * kBins +
                                                  (acrossZ ? phase(z) : 0));
        sum[bin] += h[i];
        ++count[bin];
        ++n;
        all += h[i] * h[i];
    }
    double power = 0.0;
    int phases = 0;
    for (std::size_t b = 0; b < sum.size(); ++b) {
        power += count[b] > 0 ? sum[b] * sum[b] / count[b] : 0.0;
        phases += count[b] > 0 ? 1 : 0;
    }
    return n > 0 ? std::sqrt(std::max(0.0, power - phases * all / n) / n) : 0.0;
}

// The cell of the tile under each point of the line (0 off them).
std::vector<double> cellsOf(const Tiles& tiles, const std::vector<Point>& line) {
    std::vector<double> cells(line.size(), 0.0);
    for (std::size_t n = 0; n < tiles.data.size(); ++n) {
        const terrain::TileData& t = tiles.data[n];
        const double size = terrain::tileSizeMeters(tiles.lods[n]);
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i].x >= t.originX && line[i].x < t.originX + size &&
                line[i].z >= t.originZ && line[i].z < t.originZ + size) {
                cells[i] = terrain::tileCellMeters(tiles.lods[n]);
            }
        }
    }
    return cells;
}

// A signal along the line: its autocorrelation at 2, 4 and 8 m and at the steps where the face
// crosses the cells of 8 m (8 m over the cosine and the sine of its mean angle to them), and its
// part at the lattice of the cells, most at most.
void checkLattice(const char* what, const std::vector<Point>& line, const std::vector<double>& v,
                  const std::vector<double>& cells, double most) {
    const std::vector<double> h = detrended(v, kTrend);
    const double angle = std::atan2(std::abs(line.back().z - line.front().z),
                                    std::abs(line.back().x - line.front().x));
    double rms = 0.0;
    std::printf("    %s: autocorrelation", what);
    for (const double step : {2.0, 4.0, 8.0, 8.0 / std::max(std::cos(angle), 0.05),
                              8.0 / std::max(std::sin(angle), 0.05)}) {
        if (step <= kTrend) {
            std::printf(" %.2f at %.1f m,", autocorrelation(h, step, rms), step);
        }
    }
    autocorrelation(h, 0.0, rms);
    const double part = latticePart(line, h, cells);
    std::printf(" its part at the lattice of the cells %.4f (of %.4f), at most %.4f\n", part, rms,
                most);
    TEST_CHECK(part <= most);
}

// The light of each pixel of an image of tiles: the rock under its bands, the ground kGround.
std::vector<double> lightOf(const coast_bands::TileImage& img) {
    std::vector<double> out(img.lit.size(), -1.0);
    for (int y = 0; y + 1 < kHeight; ++y) {
        for (int x = 0; x + 1 < kWidth; ++x) {
            const auto at = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
            if (img.lit[at] < 0.0) {
                continue;
            }
            const double s = static_cast<double>(img.bands[at].strata);
            const double w = std::abs(static_cast<double>(img.bands[at + 1].strata) - s) +
                             std::abs(static_cast<double>(img.bands[at + kWidth].strata) - s);
            out[at] = img.lit[at] * (img.rock[at] * coast_bands::bandsOf(img.bands[at], w) +
                                     (1.0 - img.rock[at]) * kGround);
        }
    }
    return out;
}

double metersOf(const std::vector<double>& v) {
    const auto seen = std::count_if(v.begin(), v.end(), [](double x) { return !std::isnan(x); });
    return static_cast<double>(seen) * coast_render::kStep;
}

// A wall seen from 300 m on its tiles.
void testTiles(const coast_rock::Place& place, const Eye* view) {
    const std::vector<Point> line = coast_render::lineOf(place, kHalf);
    const double length = line.empty() ? 0.0 : line.back().s;
    TEST_CHECK(length > kHalf);
    if (!(length > kHalf)) {
        return;
    }
    const Point& middle = line[line.size() / 2];
    const Eye eye = view != nullptr ? *view : eyeBefore(middle);
    const Tiles tiles = tilesOver(eye, line);
    int count = 0;
    const double stripped = strippedShare(tiles.data, line, count);
    std::printf("%s: %.0f m of face, %zu tiles, %d vertices on the face, %.1f %% without all of "
                "its rock\n",
                place.name, length, tiles.data.size(), count, 100.0 * stripped);
    TEST_CHECK(count > 100 && stripped == 0.0);
    const coast_bands::TileImage img = coast_bands::renderTiles(
        tiles.data, eye, coast_image::sunOf(eye, kSunAround, kSunHigh));
    const Along a = alongTop(lightOf(img), &img.point, eye, line);
    std::printf("  its top seen along %.0f m of it\n", metersOf(a.light));
    TEST_CHECK(metersOf(a.light) > kHalf);
    const std::vector<double> cells = cellsOf(tiles, line);
    checkLattice("the light under the top", line, a.light, cells, kLightMost);
    // A pixel of the screen of Unreal at the distance of the face; a saw kSkyPixels of it from
    // peak to peak (its root mean square the root of 12 times smaller).
    const double pixel =
        std::hypot(middle.x - eye.x, middle.z - eye.z) * std::numbers::pi / 2.0 / kScreen;
    checkLattice("the sky line", line, skyLine(tiles.data, eye, line), cells,
                 kSkyPixels * pixel / std::sqrt(12.0));
}

// The notched cliff of the view 3 at 800 m: the level 0 of the horizon, at the step of its blocks.
void testBlocks() {
    const std::vector<Point> line = coast_render::lineOf(coast_rock::kCliff, kLongHalf);
    const double length = line.empty() ? 0.0 : line.back().s;
    std::printf("%s from 800 m: %.0f m of face\n", coast_rock::kCliff.name, length);
    TEST_CHECK(length > 4.0 * kBlockStep);
    if (!(length > 4.0 * kBlockStep)) {
        return;
    }
    const coast_render::Level0 l = coast_render::level0Around(line);
    const Eye eye{429'427.2, -855'233.8, kEye, 29.1, 3.0, 50.0};
    const coast_image::Image img =
        coast_image::render(l, eye, coast_image::sunOf(eye, kSunAround, kSunHigh));
    const Along a = alongTop(img.luminance, nullptr, eye, line);
    for (const auto& [what, v] : {std::pair{"the light under the top", &a.light},
                                  std::pair{"the sky line", &a.sky}}) {
        double rms = 0.0;
        const double r = autocorrelation(detrended(*v, 2.0 * kBlockStep), kBlockStep, rms);
        std::printf("    %s: autocorrelation %.2f at the step of the blocks (%.0f m)\n", what, r,
                    kBlockStep);
        TEST_CHECK(r <= kPeriodic);
    }
}

} // namespace

int main() {
    const Eye view5{23'416'410.3, -2'119'673.8, kEye, 9.1, 3.0, 50.0};
    testTiles(coast_rock::kCalanque, &view5);
    testTiles(coast_rock::kCliff, nullptr);
    testBlocks();
    return test::result();
}
