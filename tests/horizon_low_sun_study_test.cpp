// The shadows of the GPU horizon under a low sun, a study (agent 40, commit 11): where to put the
// threshold under which its levels 0 to 3 cast shadows (Bridge/horizon_low_sun.hpp), so that
// turning them on or off there shows no jump. The orchestrator saw them only at 7 degrees on
// giants_B (1.5 % of the pixels: the long shadow of a massif of dunes over the far plain), nothing
// with the levels 0 and 1 alone, and pays their cost under any sun.
// An image of kWidth x kHeight pixels (a field of 90 degrees) of giants_B and of erg_vagues_crete,
// cast on the ground the GPU draws (the samples of the levels 0 to 3 raised to their crests,
// water_horizon::drawnLevel; the finest level holding a point; the tiles under 512 m taken as level
// 0; no curvature). A pixel whose ground lies beyond the tiles and faces the sun (the normal of the
// grid, N.L > kLit) is darkened when the horizon casts shadows if a ray from it toward the sun (at
// the heading 254 of the orchestrator's tests) meets the ground beyond the tiles first: the tiles
// cast theirs anyway. Printed: the share of such pixels at each elevation, and the elevation where
// it falls under 1/15 of its value at 7 degrees (1.5 % in Unreal would then be 0.1 %).
#include "water_horizon_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <numbers>
#include <optional>
#include <vector>

namespace {

using water_horizon::DrawnLevel;

struct Place {
    const char* name;
    double x, z, eye, heading, pitch;
};

constexpr std::array<Place, 2> kPlaces{{{"giants_B", -323600.0, 3960700.0, 1000.0, 285.0, -30.0},
                                        {"erg_vagues_crete", 11505016.0, -4964964.0, 1.8, 254.0,
                                         -10.0}}};
constexpr double kSunHeading = 254.0;
constexpr std::array<double, 9> kElevations{5.0, 7.0, 9.0, 10.0, 11.0, 12.0, 13.0, 15.0, 20.0};
constexpr int kWidth = 240;
constexpr int kHeight = 135;
constexpr double kHalfField = 45.0;
constexpr double kTiles = 512.0; // Chebyshev, around the eye
constexpr int kLevels = 4;
constexpr double kLit = 0.05;

struct V {
    double x = 0.0, y = 0.0, z = 0.0; // the core: x east, y up, z south
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V unit(V a) { return a * (1.0 / std::sqrt(dot(a, a))); }
double rad(double degrees) { return degrees * std::numbers::pi / 180.0; }

// Toward the sun at this elevation, at kSunHeading (clockwise from the north, -z).
V sunToward(double elevation) {
    const double e = rad(elevation);
    const double h = rad(kSunHeading);
    return {std::cos(e) * std::sin(h), std::sin(e), -std::cos(e) * std::cos(h)};
}

struct Sample {
    double height;
    double pitch;
    std::array<double, 2> slope;
};

// The ground of the levels 0 to 3 around the eye, each level read over the box of the sector seen
// and of the heading of the sun only (the rest of it is never reached).
class Ground {
public:
    explicit Ground(const Place& p) : eyeX_(p.x), eyeZ_(p.z) {
        const double from = std::min(p.heading - kHalfField - 10.0, kSunHeading - 15.0);
        const double to = std::max(p.heading + kHalfField + 10.0, kSunHeading + 15.0);
        std::vector<std::future<DrawnLevel>> pending;
        for (int level = 0; level < kLevels; ++level) {
            const terrain::HorizonRect full = water_horizon::rectAround(level, p.x, p.z, 128);
            const double pitch = terrain::levelPitch(water_horizon::params(), level);
            const double reach = 128.0 * pitch;
            double x0 = p.x, x1 = p.x, z0 = p.z, z1 = p.z;
            for (double a = from; a <= to; a += 2.0) {
                const double x = p.x + reach * std::sin(rad(a));
                const double z = p.z - reach * std::cos(rad(a));
                x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z);
                z1 = std::max(z1, z);
            }
            const terrain::HorizonRect rect{
                std::max(full.x0, static_cast<std::int64_t>(std::floor(x0 / pitch)) - 1),
                std::max(full.z0, static_cast<std::int64_t>(std::floor(z0 / pitch)) - 1),
                std::min(full.x1, static_cast<std::int64_t>(std::ceil(x1 / pitch)) + 2),
                std::min(full.z1, static_cast<std::int64_t>(std::ceil(z1 / pitch)) + 2)};
            // The levels sampled at once (the samplers of the core only read).
            pending.push_back(std::async(std::launch::async, [level, rect] {
                return water_horizon::drawnLevel(level, rect, false);
            }));
        }
        for (std::future<DrawnLevel>& f : pending) {
            levels_.push_back(f.get());
            for (const terrain::unreal::HorizonTexel& t : levels_.back().texels) {
                top_ = std::max(top_, static_cast<double>(t.height));
            }
        }
    }

    [[nodiscard]] std::optional<Sample> at(double x, double z) const {
        for (const DrawnLevel& l : levels_) {
            const std::optional<double> h =
                l.heightAt(x, z, terrain::unreal::CellSplit::Fixed,
                           terrain::unreal::SeaDrawing::InGround);
            if (h) {
                const auto g = l.slopeAt(x, z, terrain::unreal::CellSplit::Fixed, false);
                return Sample{*h, l.pitch, *g};
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] double top() const { return top_; }
    [[nodiscard]] bool beyondTiles(V p) const {
        return std::max(std::abs(p.x - eyeX_), std::abs(p.z - eyeZ_)) > kTiles;
    }

private:
    double eyeX_, eyeZ_;
    std::vector<DrawnLevel> levels_;
    double top_ = -1e30;
};

// The next step along a ray at q over a sample s: never over a cell, faster high above the ground.
double stepOver(const V& q, const Sample& s) {
    return std::max(0.5 * s.pitch, 0.3 * (q.y - s.height));
}

// Where the ray from o along d first meets the ground, if it does within the levels.
std::optional<V> hit(const Ground& g, V o, V d) {
    double before = 0.0;
    double t = 0.5;
    for (int n = 0; n < 100000; ++n) {
        const V q = o + d * t;
        if (q.y > g.top() && d.y >= 0.0) {
            return std::nullopt;
        }
        const std::optional<Sample> s = g.at(q.x, q.z);
        if (!s) {
            return std::nullopt;
        }
        if (q.y <= s->height) {
            double a = before, b = t;
            for (int k = 0; k < 14; ++k) {
                const double m = 0.5 * (a + b);
                const V r = o + d * m;
                const std::optional<Sample> sm = g.at(r.x, r.z);
                (sm && r.y <= sm->height ? b : a) = m;
            }
            return o + d * b;
        }
        before = t;
        t += stepOver(q, *s);
    }
    return std::nullopt;
}

// Whether the ground beyond the tiles stands between p and the sun first.
bool shadowedByHorizon(const Ground& g, V p, V sun, double pitch) {
    const V o = p + V{0.0, 0.05 * pitch, 0.0};
    double t = 0.5 * pitch;
    for (int n = 0; n < 100000; ++n) {
        const V q = o + sun * t;
        if (q.y > g.top()) {
            return false;
        }
        const std::optional<Sample> s = g.at(q.x, q.z);
        if (!s) {
            return false;
        }
        if (q.y < s->height) {
            return g.beyondTiles(q);
        }
        t += stepOver(q, *s);
    }
    return false;
}

struct Receiver {
    V p;
    V normal;
    double pitch;
};

// The pixels whose ground lies beyond the tiles, with its point and the normal of the grid.
std::vector<Receiver> receivers(const Ground& g, const Place& p) {
    const std::optional<Sample> under = g.at(p.x, p.z);
    TEST_CHECK(under.has_value());
    const V eye{p.x, (under ? under->height : 0.0) + p.eye, p.z};
    const double h = rad(p.heading);
    const double a = rad(p.pitch);
    const V forward{std::cos(a) * std::sin(h), std::sin(a), -std::cos(a) * std::cos(h)};
    const V right{std::cos(h), 0.0, std::sin(h)};
    const V up{-std::sin(a) * std::sin(h), std::cos(a), std::sin(a) * std::cos(h)};
    const double half = std::tan(rad(kHalfField));
    std::vector<Receiver> out;
    for (int py = 0; py < kHeight; ++py) {
        for (int px = 0; px < kWidth; ++px) {
            const double sx = (2.0 * (px + 0.5) / kWidth - 1.0) * half;
            const double sy = (1.0 - 2.0 * (py + 0.5) / kHeight) * half * kHeight / kWidth;
            const std::optional<V> q = hit(g, eye, unit(forward + right * sx + up * sy));
            if (!q || !g.beyondTiles(*q)) {
                continue;
            }
            const std::optional<Sample> s = g.at(q->x, q->z);
            out.push_back({*q, unit(V{-s->slope[0], 1.0, -s->slope[1]}), s->pitch});
        }
    }
    return out;
}

// The share of the image darkened by the shadows of the horizon, at each elevation.
std::array<double, kElevations.size()> darkened(const Ground& g,
                                                const std::vector<Receiver>& seen) {
    std::array<double, kElevations.size()> out{};
    for (std::size_t e = 0; e < kElevations.size(); ++e) {
        const V sun = sunToward(kElevations[e]);
        int count = 0;
        for (const Receiver& r : seen) {
            if (dot(r.normal, sun) > kLit && shadowedByHorizon(g, r.p, sun, r.pitch)) {
                ++count;
            }
        }
        out[e] = 100.0 * count / (kWidth * kHeight);
    }
    return out;
}

} // namespace

int main() {
    double threshold = 0.0;
    for (const Place& p : kPlaces) {
        const auto start = std::chrono::steady_clock::now();
        const Ground g(p);
        const auto sampled = std::chrono::steady_clock::now();
        const std::vector<Receiver> seen = receivers(g, p);
        const auto share = darkened(g, seen);
        const auto seconds = [](auto from, auto to) {
            return std::chrono::duration<double>(to - from).count();
        };
        std::printf("%s (samples %.1f s, rays %.1f s): %.1f %% of the image on the horizon beyond "
                    "the tiles; darkened by its shadows, sun at the heading %.0f:",
                    p.name, seconds(start, sampled),
                    seconds(sampled, std::chrono::steady_clock::now()), 100.0 * static_cast<double>(seen.size()) / (kWidth * kHeight),
                    kSunHeading);
        double below = kElevations.back();
        for (std::size_t e = kElevations.size(); e-- > 0 && share[e] <= share[1] / 15.0;) {
            below = kElevations[e]; // from there up, always under the bar
        }
        for (std::size_t e = 0; e < kElevations.size(); ++e) {
            std::printf(" %.0f deg %.3f %%%s", kElevations[e], share[e],
                        e + 1 < kElevations.size() ? "," : "\n");
        }
        std::printf("  under 1/15 of its value at 7 degrees from %.0f degrees on\n", below);
        threshold = std::max(threshold, below);
        TEST_CHECK(!seen.empty());
        TEST_CHECK(share.front() >= share.back());
    }
    std::printf("the threshold that shows no jump at both places: %.0f degrees\n", threshold);
    return test::result();
}
