// The walls of the coasts as the tiles draw them (agent 33), on the default world with its planet
// file (coast_planet_support.hpp): seen from the sea, a cliff is no straight wall of one height,
// and nothing along a cliff or a rocky coast comes back at a regular step.
// A trace runs along the coast on the line of the coast signal of the world through a face
// (parallel to its shore, along the walls of its inlets), a sample every step; across the top of
// the tiles there, the line of the face (where the top falls most, read between the meters where
// it crosses the middle of the fall), its angle to the trace (which a low sun lights and shades:
// the blocks of the face) and its top a few meters behind its line. Of each, the part a view
// shows: less its mean around each sample over a bell a quarter of the width of the view wide. A
// trace stops at a river (its valley cuts the cliff) or where no face lies, and counts from a
// least number of samples on.
// - From afar (kFar: the tiles of LOD 3, cells of 8 m, every 4 m; the views from 300 m out to sea
//   and beyond, 300 m of the coast wide): along a cliff, the line of the face moves in and out by
//   kLineLeast meters at least (the standard deviation of that part; kLineSpread over all the
//   cliffs, as the root of the mean of their squares: on a cape whose platform leaves no room the
//   face stands halfway up its slope, and moves half as much), the face turns by kTurnSpread
//   degrees at least, its top rises and dips by kTopSpread of its median at least.
// - None of the three repeats, from afar and from near (kNear: the tiles of LOD 1, cells of 2 m,
//   every meter; 60 m of the coast wide, the joints of the rock): once its autocorrelation has
//   fallen below 0, it never comes back above kRebound (as along the tops of the cliffs in
//   coast_patterns_test).
// At the notched cliff of the tests of the rock (coast_rock_support: the place of the tests shows
// it too) from afar and from near, kTraces cliffs drawn over the world (kinds of coast from
// coastAt) from afar, and the notched rocky coast from near only: it stands in a group of coves,
// along whose walls the face keeps its line (coast_profile::Along).
#include "coast_planet_support.hpp"
#include "coast_rock_support.hpp"
#include "coast_zone_support.hpp"

#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using terrain::CoastKind;

// A scale of the traces: the LOD of the tiles, the step between samples, the reach across the
// trace on either side, the fall over a few meters on either side of a point that makes a face
// (fall meters, leastFall meters at least), the top edge meters behind the line, the width of a
// view, the least number of samples of a trace and its length on either side of a place.
struct Scale {
    const char* name;
    int lod;
    double step;
    double reach;
    double fall;
    double leastFall;
    double edge;
    double seen;
    std::size_t least;
    double half;
};
constexpr Scale kFar{"from afar", 3, 4.0, 80.0, 4.0, 3.0, 10.0, 300.0, 300, 1'200.0};
constexpr Scale kNear{"from near", 1, 1.0, 50.0, 2.0, 3.0, 4.0, 60.0, 150, 200.0};
constexpr double kLength = 2'400.0;
constexpr int kTraces = 4;
constexpr double kLineLeast = 3.5;
constexpr double kLineSpread = 5.5;
constexpr double kTurnSpread = 10.0;
constexpr double kTopSpread = 0.045;
constexpr double kRebound = 0.6;
constexpr double kPi = 3.14159265358979323846;

const terrain::Heightfield& field() {
    return coast_planet::field();
}

// The coast signal of the world (the inlets cut in it: its lines run along their walls).
double signalAt(double x, double z) {
    const terrain::WorldSize& w = field().world();
    return field()
        .coastAt(terrain::wrapCoordinate(x, w.x), terrain::wrapCoordinate(z, w.z))
        .signal;
}

// The gradient of the coast signal, per meter.
std::array<double, 2> gradientAt(double x, double z) {
    const double v = signalAt(x, z);
    return {signalAt(x + 1.0, z) - v, signalAt(x, z + 1.0) - v};
}

// (x, z) moved along the gradient of the coast signal to where it is v.
void toLevel(double& x, double& z, double v) {
    for (int i = 0; i < 8; ++i) {
        const double off = signalAt(x, z) - v;
        const auto [gx, gz] = gradientAt(x, z);
        const double g2 = gx * gx + gz * gz;
        if (!(std::abs(off) > 1e-3) || !(g2 > 0.0)) {
            return;
        }
        x -= off * gx / g2;
        z -= off * gz / g2;
    }
}

// The face across the coast at (x, z) toward the sea (ax, az), from -from to to meters: its line
// (meters toward the sea) and its top.
struct Across {
    bool found = false;
    double at = 0.0;
    double top = 0.0;
};

Across acrossAt(coast_rock::Tiles& tiles, const Scale& k, double x, double z, double ax, double az,
                double from, double to) {
    const auto n = static_cast<std::size_t>(from + to);
    std::vector<double> h(n + 1);
    for (std::size_t i = 0; i <= n; ++i) {
        const double s = static_cast<double>(i) - from;
        h[i] = std::max(0.0, tiles.topAt(x + s * ax, z + s * az));
    }
    const auto fall = static_cast<std::size_t>(k.fall);
    const auto edge = static_cast<std::size_t>(k.edge);
    double most = k.leastFall;
    std::size_t at = 0;
    for (std::size_t i = std::max(fall, edge); i + edge <= n; ++i) {
        const double drop = h[i - fall] - h[i + fall];
        if (drop > most) {
            most = drop;
            at = i;
        }
    }
    if (at == 0) {
        return {};
    }
    const double middle = 0.5 * (h[at - edge] + h[at + edge]);
    Across out{true, static_cast<double>(at) - from, h[at - edge]};
    for (std::size_t i = at - fall; i < at + fall; ++i) {
        if (h[i] >= middle && h[i + 1] < middle) {
            out.at = static_cast<double>(i) - from + (h[i] - middle) / (h[i] - h[i + 1]);
            break;
        }
    }
    return out;
}

struct Trace {
    std::vector<double> line;
    std::vector<double> top;
};

// From the face at (x, z), count samples along the line of the coast signal there, toward the
// tangent (tx, tz) (to the right of the sea where 0), until a river or no face.
Trace traceFrom(coast_rock::Tiles& tiles, const Scale& k, double x, double z, int count,
                double tx, double tz) {
    const double level = signalAt(x, z);
    Trace out;
    for (int i = 0; i < count; ++i) {
        toLevel(x, z, level);
        const auto [gx, gz] = gradientAt(x, z);
        const double g = std::hypot(gx, gz);
        if (!(g > 0.0) || coast_planet::nearRiver(x, z)) {
            break;
        }
        const double ax = -gx / g;
        const double az = -gz / g;
        const double sign = (tx == 0.0 && tz == 0.0) || -az * tx + ax * tz >= 0.0 ? 1.0 : -1.0;
        tx = -az * sign;
        tz = ax * sign;
        const Across a = acrossAt(tiles, k, x, z, ax, az, k.reach, k.reach);
        if (!a.found) {
            break;
        }
        out.line.push_back(a.at);
        out.top.push_back(a.top);
        x += k.step * tx;
        z += k.step * tz;
    }
    return out;
}

// The traces from the face at (x, z) on either side of it, end to end.
Trace bothWays(coast_rock::Tiles& tiles, const Scale& k, double x, double z, double tx, double tz) {
    const auto count = static_cast<int>(k.half / k.step);
    Trace back = traceFrom(tiles, k, x, z, count, -tx, -tz);
    const Trace ahead = traceFrom(tiles, k, x, z, count, tx, tz);
    std::reverse(back.line.begin(), back.line.end());
    std::reverse(back.top.begin(), back.top.end());
    if (!ahead.line.empty()) { // its first sample is the last of back
        back.line.insert(back.line.end(), ahead.line.begin() + 1, ahead.line.end());
        back.top.insert(back.top.end(), ahead.top.begin() + 1, ahead.top.end());
    }
    return back;
}

double meanOf(const std::vector<double>& v) {
    double sum = 0.0;
    for (const double a : v) {
        sum += a;
    }
    return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
}

// v less its mean around each sample, weighted as a bell a quarter of the view wide (a bell, not
// a box: the filter adds no ripple of its own).
std::vector<double> seenPart(const std::vector<double>& v, const Scale& k) {
    const double sigma = 0.25 * k.seen / k.step;
    const auto half = static_cast<std::ptrdiff_t>(3.0 * sigma);
    const auto n = static_cast<std::ptrdiff_t>(v.size());
    std::vector<double> out;
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        double sum = 0.0;
        double weights = 0.0;
        for (std::ptrdiff_t j = std::max<std::ptrdiff_t>(0, i - half);
             j <= std::min(n - 1, i + half); ++j) {
            const double u = static_cast<double>(j - i) / sigma;
            const double w = std::exp(-0.5 * u * u);
            sum += w * v[static_cast<std::size_t>(j)];
            weights += w;
        }
        out.push_back(v[static_cast<std::size_t>(i)] - sum / weights);
    }
    return out;
}

double deviationOf(const std::vector<double>& v) {
    const double mean = meanOf(v);
    double sum = 0.0;
    for (const double a : v) {
        sum += (a - mean) * (a - mean);
    }
    return v.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(v.size()));
}

// The highest autocorrelation of v at a lag beyond the one where it first falls below 0, up to a
// quarter of its length (coast_patterns_test).
double reboundOf(const std::vector<double>& v) {
    const double mean = meanOf(v);
    double var = 0.0;
    for (const double a : v) {
        var += (a - mean) * (a - mean);
    }
    const auto at = [&](std::size_t lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < v.size(); ++i) {
            sum += (v[i] - mean) * (v[i + lag] - mean);
        }
        return var > 0.0 ? sum / var * static_cast<double>(v.size()) /
                               static_cast<double>(v.size() - lag)
                         : 0.0;
    };
    std::size_t lag = 1;
    while (lag < v.size() / 4 && at(lag) > 0.0) {
        ++lag;
    }
    double most = -1.0;
    for (; lag < v.size() / 4; ++lag) {
        most = std::max(most, at(lag));
    }
    return most;
}

// The angle of the face to the trace, degrees, over two samples on either side of each.
std::vector<double> turnsOf(const std::vector<double>& line, const Scale& k) {
    std::vector<double> out;
    for (std::size_t i = 2; i + 2 < line.size(); ++i) {
        out.push_back(std::atan((line[i + 2] - line[i - 2]) / (4.0 * k.step)) * 180.0 / kPi);
    }
    return out;
}

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// The squares of the spreads of the lines of the cliffs from afar, and their count.
double lineSquares = 0.0;
int lines = 0;

// The checks of a trace (its spreads if spreads); false if too short to count.
bool checkTrace(const char* name, const Trace& t, const Scale& k, bool spreads) {
    std::printf("%s %s: %zu samples", name, k.name, t.line.size());
    if (t.line.size() < k.least) {
        std::printf(" (too short: not counted)\n");
        return false;
    }
    const std::vector<double> line = seenPart(t.line, k);
    const std::vector<double> turns = seenPart(turnsOf(t.line, k), k);
    const std::vector<double> tops = seenPart(t.top, k);
    const double median = medianOf(t.top);
    const double lineSpread = deviationOf(line);
    const double turn = deviationOf(turns);
    const double top = median > 0.0 ? deviationOf(tops) / median : 0.0;
    const std::array<double, 3> rebound{reboundOf(line), reboundOf(turns), reboundOf(tops)};
    std::printf(", within %.0f m: its line in and out by %.1f m, its face turning by %.1f "
                "degrees, its top up and down by %.1f %% of %.1f m; autocorrelation back up to "
                "%.2f, %.2f, %.2f\n",
                k.seen, lineSpread, turn, 100.0 * top, median, rebound[0], rebound[1],
                rebound[2]);
    TEST_CHECK(!spreads || lineSpread >= kLineLeast);
    lineSquares += spreads ? lineSpread * lineSpread : 0.0;
    lines += spreads ? 1 : 0;
    TEST_CHECK(!spreads || turn >= kTurnSpread);
    TEST_CHECK(!spreads || top >= kTopSpread);
    TEST_CHECK(*std::max_element(rebound.begin(), rebound.end()) <= kRebound);
    return true;
}

// The places of the tests of the rock.
void testPlaces() {
    for (const Scale* k : {&kFar, &kNear}) {
        coast_rock::Tiles tiles(coast_planet::tiles(), k->lod);
        for (const coast_rock::Place* p : {&coast_rock::kCliff, &coast_rock::kRocky}) {
            const bool cliff = p == &coast_rock::kCliff;
            if (!cliff && k == &kFar) {
                continue;
            }
            const coast_rock::Face f = coast_rock::faceAt(field(), *p);
            TEST_CHECK(f.found);
            const Trace t = bothWays(tiles, *k, f.x, f.z, -static_cast<double>(f.face.airZ),
                                     static_cast<double>(f.face.airX));
            TEST_CHECK(checkTrace(p->name, t, *k, cliff && k == &kFar));
        }
    }
}

// Along cliffs drawn over the world from afar: from the shore of the geography up the coast
// signal, the face found across the tiles.
void testCliffs() {
    coast_zone::Draws draws(0x33C0A57E5ULL);
    const terrain::WorldSize& w = field().world();
    coast_rock::Tiles tiles(coast_planet::tiles(), kFar.lod);
    int traces = 0;
    for (int tries = 0; traces < kTraces && tries < 200'000; ++tries) {
        double x = (draws.next() - 0.5) * w.x;
        double z = (draws.next() - 0.5) * w.z;
        const terrain::CoastSample c = field().coastAt(x, z);
        if (!(c.base > 0.0 && c.base < 2'000.0) || c.weight(CoastKind::Cliff) < 0.7f) {
            continue;
        }
        toLevel(x, z, 0.0);
        const auto [gx, gz] = gradientAt(x, z);
        const double g = std::hypot(gx, gz);
        if (!(g > 0.0) || coast_planet::nearRiver(x, z)) {
            continue;
        }
        const Across a =
            acrossAt(tiles, kFar, x, z, -gx / g, -gz / g, 3.0 * kFar.reach, kFar.reach);
        if (!a.found) {
            continue;
        }
        x -= a.at * gx / g;
        z -= a.at * gz / g;
        const Trace t =
            traceFrom(tiles, kFar, x, z, static_cast<int>(kLength / kFar.step), 0.0, 0.0);
        char name[96];
        std::snprintf(name, sizeof name, "the cliffs from (%.0f, %.0f)", x, z);
        traces += checkTrace(name, t, kFar, true) ? 1 : 0;
    }
    TEST_CHECK(traces == kTraces);
}

} // namespace

int main() {
    testPlaces();
    testCliffs();
    const double line = lines > 0 ? std::sqrt(lineSquares / lines) : 0.0;
    std::printf("the lines of the %d cliffs from afar: in and out by %.1f m\n", lines, line);
    TEST_CHECK(line >= kLineSpread);
    return test::result();
}
