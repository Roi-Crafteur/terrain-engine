// The canyons of the plateaus (agent 46, tableland_canyons.hpp) against real cases, on the default
// world with the plateaus switched on: the deepest canyons of the thickest plateaus, each measured
// up a section from its floor to its rim.
//   - at least one as the Grand Canyon: 1.2 km deep at least, 3 bands of cliffs, a face of the rock
//     of 120 m (the Redwall, about 150 m);
//   - its walls of varied bands (no comb of contour lines): a great cliff of 120 to 200 m, a
//     caprock under the rim, a platform; the standard deviation and the coefficient of variation
//     of the heights of its bands printed, the variation 0.5 at least;
//   - temples: at least one standing in each canyon 1.2 km deep or more.
#include "pattern_support.hpp"
#include "tablelands_fields_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using tablelands_fields::field;
using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;
using terrain::tableland_relief::Fields;

constexpr double kCell = 250.0;
// The canyons measured inland, kInland meters at least from the escarpment (at their mouth their
// walls are those of the escarpment).
constexpr double kInland = 6'000.0;
// The canyon: at least kCanyonDeep meters deep, kLeastBands bands of cliffs (runs steeper than
// kCliffSlope rising kLeastBand meters at least), one face kTallFace meters high, the heights of its
// bands varying by kLeastVariation at least, one of them kGreatLow meters at least; a caprock of
// kCapLeast meters within kRimReach meters of the rim; a platform kPlatformLeast meters long under
// kPlatformSlope, between kPlatformHigh and kPlatformLow of its depth.
constexpr double kCanyonDeep = 1200.0;
constexpr int kLeastBands = 3;
constexpr double kCliffSlope = 1.0;
constexpr double kLeastBand = 10.0;
constexpr double kTallFace = 120.0;
constexpr double kLeastVariation = 0.5;
constexpr double kGreatLow = 120.0;
constexpr double kCapLeast = 30.0;
constexpr double kRimReach = 150.0;
// A section reaches the top of the plateau where the canyons cut it less than kRimCut meters there
// (no temple, no outlier: their caprocks are not the caprock of the canyon).
constexpr double kRimCut = 10.0;
constexpr double kPlatformLeast = 250.0;
constexpr double kPlatformSlope = 0.15;
constexpr double kPlatformHigh = 0.3;
constexpr double kPlatformLow = 0.85;
// The temples: groups of at least kTempleCells cells of kTempleCell meters where a temple stands,
// within kTempleReach meters of the floor.
constexpr std::size_t kTempleCells = 3;
constexpr double kTempleCell = 100.0;
constexpr int kTempleReach = 40; // cells on each side

// The deepest floor of a canyon in a box of side cells of kCell meters around (x, z).
std::array<double, 3> deepestFloor(double x0, double z0, int side) {
    std::array<double, 3> best{x0, z0, 0.0};
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const double x = x0 + kCell * (i - side / 2);
            const double z = z0 + kCell * (j - side / 2);
            const Fields f = fieldsAt(x, z);
            if (f.floor >= 1.0 && f.canyon > best[2] && f.scarp > kInland) {
                best = {x, z, f.canyon};
            }
        }
    }
    return best;
}

// A section from the floor of a canyon up its wall to its rim, kSection meters at most, a height
// every kSectionStep meters: the direction that rises the most within it.
constexpr double kSection = 7'000.0;
constexpr double kSectionStep = 5.0;
struct Section {
    double dx = 1.0;
    double dz = 0.0;
    std::vector<double> heights;
    std::size_t rim = 0; // the index of its highest point
};

// The sections from (x, z) in 24 directions.
std::vector<Section> sectionsFrom(double x, double z) {
    std::vector<Section> out;
    for (int k = 0; k < 24; ++k) {
        const double a = 2.0 * 3.141592653589793 * k / 24.0;
        Section s{std::cos(a), std::sin(a), {}, 0};
        for (double d = 0.0; d <= kSection; d += kSectionStep) {
            s.heights.push_back(heightAt(x + s.dx * d, z + s.dz * d));
            s.rim = s.heights.back() > s.heights[s.rim] ? s.heights.size() - 1 : s.rim;
        }
        out.push_back(std::move(s));
    }
    return out;
}

// The faces of the rock in three dimensions along the section from (x, z) and along sections beside
// it, up to 400 m on each side: the tallest.
double tallestFace(double x, double z, const Section& s) {
    double face = 0.0;
    for (int side = -8; side <= 8; ++side) {
        const double ox = -s.dz * 50.0 * side;
        const double oz = s.dx * 50.0 * side;
        for (std::size_t i = 0; i <= s.rim; ++i) {
            const double d = kSectionStep * static_cast<double>(i);
            const terrain::RockColumn rock = field().rockAt(x + ox + s.dx * d, z + oz + s.dz * d);
            for (std::size_t k = 0; k < rock.faceCount; ++k) {
                face = std::max(face, static_cast<double>(rock.faces[k].top - rock.faces[k].foot));
            }
        }
    }
    return face;
}

// What a section up the wall of a canyon shows.
struct Wall {
    double depth = 0.0;
    std::vector<double> bands; // the heights of its bands of cliffs, from the floor up
    double tallest = 0.0;      // the tallest band
    double face = 0.0;         // the tallest face of the rock in three dimensions
    double slope500 = 0.0;     // the steepest 500 m
    double cap = 0.0;          // the band under the rim (its caprock)
    double platform = 0.0;     // the longest stretch under kPlatformSlope at mid-depth, meters
    bool rimOnTop = false;     // its rim on the top of the plateau (no temple, no other canyon)
    double rimX = 0.0;
    double rimZ = 0.0;
};

// The bands of cliffs up a section to its rim: runs steeper than kCliffSlope.
void addBands(const Section& s, Wall& w) {
    double run = 0.0;
    double top = 0.0; // the height of the top of the last band
    for (std::size_t i = 1; i <= s.rim + 1; ++i) {
        const double climb = i <= s.rim ? s.heights[i] - s.heights[i - 1] : 0.0;
        if (climb / kSectionStep >= kCliffSlope) {
            run += climb;
            top = s.heights[std::min(i, s.rim)];
            continue;
        }
        if (run >= kLeastBand) {
            w.bands.push_back(run);
            w.tallest = std::max(w.tallest, run);
            w.cap = s.heights[s.rim] - top <= kRimReach ? run : w.cap;
        }
        run = 0.0;
    }
}

// The longest stretch of a section under kPlatformSlope over 25 m, between kPlatformHigh and
// kPlatformLow of the depth under its rim.
double platformOf(const Section& s, double depth) {
    const double rim = s.heights[s.rim];
    const auto per25 = static_cast<std::size_t>(25.0 / kSectionStep);
    double longest = 0.0;
    double run = 0.0;
    for (std::size_t i = per25; i <= s.rim; ++i) {
        const double under = rim - s.heights[i];
        const bool flat = std::abs(s.heights[i] - s.heights[i - per25]) / 25.0 < kPlatformSlope &&
                          under > kPlatformHigh * depth && under < kPlatformLow * depth;
        run = flat ? run + kSectionStep : 0.0;
        longest = std::max(longest, run);
    }
    return longest;
}

Wall wallOf(double x, double z, const Section& s) {
    Wall w;
    w.depth = s.heights[s.rim] - s.heights[0];
    const double reach = kSectionStep * static_cast<double>(s.rim);
    w.rimX = x + s.dx * reach;
    w.rimZ = z + s.dz * reach;
    const Fields rim = fieldsAt(w.rimX, w.rimZ);
    w.rimOnTop = !rim.temple && !rim.outlier && rim.canyon < kRimCut && rim.profile >= 0.999;
    addBands(s, w);
    const auto per500 = static_cast<std::size_t>(500.0 / kSectionStep);
    for (std::size_t i = per500; i <= s.rim; ++i) {
        w.slope500 = std::max(w.slope500, std::abs(s.heights[i] - s.heights[i - per500]) / 500.0);
    }
    w.platform = platformOf(s, w.depth);
    return w;
}

// How many of the units of the Grand Canyon a wall shows (its face aside).
int unitsOf(const Wall& w) {
    return (w.depth >= kCanyonDeep ? 1 : 0) +
           (static_cast<int>(w.bands.size()) >= kLeastBands ? 1 : 0) +
           (pattern_test::variation(w.bands) >= kLeastVariation ? 1 : 0) +
           (w.tallest >= kGreatLow ? 1 : 0) + (w.cap >= kCapLeast ? 1 : 0) +
           (w.platform >= kPlatformLeast ? 1 : 0);
}

// The section of a canyon from its floor (x, z) that shows the most of those units, its faces
// measured: of those climbing to the top of the plateau (onTop), or of all.
Wall bestWall(double x, double z, bool onTop) {
    const std::vector<Section> sections = sectionsFrom(x, z);
    std::size_t best = 0;
    Wall chosen;
    bool found = false;
    for (std::size_t k = 0; k < sections.size(); ++k) {
        const Wall w = wallOf(x, z, sections[k]);
        if (onTop && !w.rimOnTop) {
            continue;
        }
        if (!found || unitsOf(w) > unitsOf(chosen) ||
            (unitsOf(w) == unitsOf(chosen) && w.depth > chosen.depth)) {
            found = true;
            chosen = w;
            best = k;
        }
    }
    chosen.face = tallestFace(x, z, sections[best]);
    return chosen;
}

// The temples around the floor (x, z): groups of cells where one stands.
int templesAround(double x0, double z0) {
    const int side = 2 * kTempleReach + 1;
    std::vector<int> cells(static_cast<std::size_t>(side * side), 0);
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const Fields f = fieldsAt(x0 + kTempleCell * (i - kTempleReach),
                                      z0 + kTempleCell * (j - kTempleReach));
            cells[static_cast<std::size_t>(j * side + i)] = f.temple ? 1 : 0;
        }
    }
    int groups = 0;
    std::vector<int> stack;
    for (int start = 0; start < side * side; ++start) {
        if (cells[static_cast<std::size_t>(start)] != 1) {
            continue;
        }
        std::size_t count = 0;
        stack.push_back(start);
        cells[static_cast<std::size_t>(start)] = 2;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            ++count;
            for (const int d : {1, -1, side, -side}) {
                const int nc = c + d;
                const bool row = d == 1 || d == -1 ? nc / side == c / side : true;
                if (nc >= 0 && nc < side * side && row && cells[static_cast<std::size_t>(nc)] == 1) {
                    cells[static_cast<std::size_t>(nc)] = 2;
                    stack.push_back(nc);
                }
            }
        }
        groups += count >= kTempleCells ? 1 : 0;
    }
    return groups;
}

// The deepest canyons of the thickest plateaus.
void testCanyons(const tablelands_fields::Sites& sites) {
    int cases = 0;
    int deep = 0;
    int templed = 0;
    for (std::size_t t = 0; t < sites.thick.size() && t < 12; ++t) {
        const std::array<double, 3> f = deepestFloor(sites.thick[t][0], sites.thick[t][1], 120);
        if (!(f[2] > 0.0)) {
            continue;
        }
        const Wall any = bestWall(f[0], f[1], false);
        const Wall w = bestWall(f[0], f[1], true);
        if (!any.rimOnTop) {
            const Fields r = fieldsAt(any.rimX, any.rimZ);
            std::printf("  (of all the sections: a caprock of %.0f m under a rim at (%.0f, %.0f) %s; "
                        "the sections up to the top of the plateau kept)\n",
                        any.cap, any.rimX, any.rimZ,
                        r.temple    ? "on a temple"
                        : r.outlier ? "on an outlier"
                        : r.canyon >= kRimCut ? "in another canyon"
                                              : "off the top");
        }
        const double variation = pattern_test::variation(w.bands);
        double mean = 0.0;
        for (const double b : w.bands) {
            mean += b / static_cast<double>(w.bands.size());
        }
        const bool isDeep = w.depth >= kCanyonDeep;
        const bool ok = isDeep && static_cast<int>(w.bands.size()) >= kLeastBands &&
                        w.face >= kTallFace && variation >= kLeastVariation &&
                        w.tallest >= kGreatLow && w.cap >= kCapLeast &&
                        w.platform >= kPlatformLeast;
        const int temples = isDeep ? templesAround(f[0], f[1]) : 0;
        cases += ok ? 1 : 0;
        deep += isDeep ? 1 : 0;
        templed += isDeep && temples >= 1 ? 1 : 0;
        std::printf("canyon at (%.0f, %.0f): %.0f m deep, %zu bands of cliffs (the tallest %.0f m, "
                    "their heights varying by %.2f, %.0f m the standard deviation), a caprock of "
                    "%.0f m, a platform of %.0f m, the tallest face %.0f m, the steepest 500 m %.0f "
                    "degrees, %d temples, its rim at (%.0f, %.0f)%s\n",
                    f[0], f[1], w.depth, w.bands.size(), w.tallest, variation, variation * mean,
                    w.cap, w.platform, w.face, std::atan(w.slope500) * 57.29577951, temples,
                    w.rimX, w.rimZ, ok ? " (the case)" : "");
    }
    std::printf("canyons: %d of them 1.2 km deep or more, %d as the Grand Canyon, %d with temples\n",
                deep, cases, templed);
    TEST_CHECK(cases >= 1);
    TEST_CHECK(deep >= 1 && templed == deep);
}

} // namespace

int main() {
    const tablelands_fields::Sites sites = tablelands_fields::findSites();
    TEST_CHECK(!sites.thick.empty());
    testCanyons(sites);
    return test::result();
}
