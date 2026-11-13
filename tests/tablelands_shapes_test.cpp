// The shapes of the plateaus (agent 46, tableland_relief.hpp) against real cases, on the default
// world with the plateaus switched on:
//   - buttes beyond the escarpments of the dry plateaus, 80 to 400 m over the low ground around
//     them, their walls steeper than 60 degrees (Monument Valley: Merrick Butte, about 300 m);
//   - a canyon at least 1.2 km deep, its walls in at least 3 bands of cliffs, one face at least
//     120 m high (the Grand Canyon: 1.6 km deep, the Redwall about 150 m);
//   - the tops never flat (the standard deviation of the heights over 500 m at least 1.5 m);
//   - the range of the heights and the slope over 500 m in the zone, printed; nothing at sea.
// Also: the share of the plateaus Tablelands::tablelandShare gives is that of at, bit for bit.
#include "tablelands_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/tablelands.hpp"

#include "tableland_relief.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::tableland_relief::Fields;
using terrain::tableland_relief::Province;

// The seeds of the Heightfield (heightfield.cpp: the layer kTablelands, the stream
// kTablelandRelief).
constexpr std::uint64_t kTablelandsLayer = 21;
constexpr std::uint64_t kReliefStream = 1001;

// The buttes: 80 to 400 m high, their walls steeper than tan(60 degrees), at least kLeastButtes of
// them; a butte holds at most kButteCells cells of kCell meters of top.
constexpr double kButteLow = 80.0;
constexpr double kButteHigh = 400.0;
constexpr double kSteep60 = 1.7320508;
constexpr int kLeastButtes = 5;
constexpr std::size_t kButteCells = 12;
constexpr double kCell = 250.0;
// The canyon: at least kCanyonDeep meters deep, kLeastBands bands of cliffs (runs steeper than
// kCliffSlope rising kLeastBand meters at least), one face kTallFace meters high.
constexpr double kCanyonDeep = 1200.0;
constexpr int kLeastBands = 3;
constexpr double kCliffSlope = 1.0;
constexpr double kLeastBand = 10.0;
constexpr double kTallFace = 120.0;
// The tops: their standard deviation over 500 m at least kLeastSpread meters, in kTopsShare of
// the windows at least.
constexpr double kLeastSpread = 1.5;
constexpr double kTopsShare = 0.95;

const Heightfield& field() {
    return tablelands_test::field();
}

double wrapX(double x) {
    return terrain::noise::wrapCanonical(x, field().world().x);
}
double wrapZ(double z) {
    return terrain::noise::wrapCanonical(z, field().world().z);
}

const terrain::tableland_relief::Noises& noises() {
    static const terrain::tableland_relief::Noises n = terrain::tableland_relief::makeNoises(
        terrain::noise::deriveSeed(
            terrain::noise::deriveSeed(terrain::kDefaultWorldSeed, kTablelandsLayer),
            kReliefStream),
        field().world());
    return n;
}

// The provinces at (x, z) as the Heightfield reads them (heightfield_tablelands.cpp).
Province provinceAt(double x, double z) {
    const terrain::Tablelands& t = *field().tablelands();
    const double cx = wrapX(x);
    const double cz = wrapZ(z);
    const terrain::GeographySample g = field().geography()->sampleAt(cx, cz);
    const double step = terrain::tableland_relief::kShareStep;
    return {t.at(cx, cz, g), t.tablelandShare(wrapX(cx + step), cz, g.coast),
            t.tablelandShare(cx, wrapZ(cz + step), g.coast)};
}

Fields fieldsAt(double x, double z) {
    return terrain::tableland_relief::fieldsAt(noises(), wrapX(x), wrapZ(z), provinceAt(x, z));
}

double heightAt(double x, double z) {
    return static_cast<double>(field().sampleAt(x, z).height);
}

// The edges of the dry plateaus: points of a lattice where the share of the plateaus is that of
// their escarpments, in the dry lands; and the hearts of the thickest plateaus.
struct Sites {
    std::vector<std::array<double, 2>> edges;
    std::vector<std::array<double, 2>> thick;
    std::size_t checked = 0; // points where tablelandShare was checked against at
    std::size_t wrong = 0;
};

Sites findSites() {
    Sites s;
    const terrain::WorldSize& w = field().world();
    constexpr double kStep = 32'768.0;
    for (double z = -0.5 * w.z + 0.5 * kStep; z < 0.5 * w.z; z += kStep) {
        for (double x = -0.5 * w.x + 0.5 * kStep; x < 0.5 * w.x; x += kStep) {
            const terrain::GeographySample g = field().geography()->sampleAt(x, z);
            const terrain::TablelandPoint p = field().tablelands()->at(x, z, g);
            if (!(p.tableland > 0.0f)) {
                continue;
            }
            ++s.checked;
            s.wrong += field().tablelands()->tablelandShare(x, z, g.coast) != p.tableland;
            if (p.tableland > 0.4f && p.tableland < 0.8f && p.arid > 0.6f) {
                s.edges.push_back({x, z});
            }
            if (p.tableland > 0.99f && fieldsAt(x, z).thickness > 1450.0) {
                s.thick.push_back({x, z});
            }
        }
    }
    return s;
}

// The steepest slope over 2 m along the ray from (x, z) in direction (dx, dz), up to length.
double steepestAlong(double x, double z, double dx, double dz, double length) {
    constexpr double kStepMeters = 2.0;
    double steepest = 0.0;
    double previous = heightAt(x, z);
    for (double s = kStepMeters; s <= length; s += kStepMeters) {
        const double h = heightAt(x + dx * s, z + dz * s);
        steepest = std::max(steepest, std::abs(h - previous) / kStepMeters);
        previous = h;
    }
    return steepest;
}

struct Butte {
    double x = 0.0;
    double z = 0.0;
    double height = 0.0;
    double wall = 0.0; // the median of the steepest slopes of its sides
};

// The outliers of the box of side cells cells around (x, z): their tops (profile 1, beyond the
// escarpment), as connected groups of cells; the small ones are buttes.
std::vector<Butte> buttesAround(double x0, double z0, int side) {
    std::vector<int> top(static_cast<std::size_t>(side * side), 0);
    std::vector<double> low;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const double x = x0 + kCell * (i - side / 2);
            const double z = z0 + kCell * (j - side / 2);
            const Fields f = fieldsAt(x, z);
            top[static_cast<std::size_t>(j * side + i)] =
                f.profile >= 0.999 && f.scarp < -300.0 && f.canyon == 0.0 ? 1 : 0;
        }
    }
    std::vector<Butte> out;
    std::vector<int> stack;
    for (int start = 0; start < side * side; ++start) {
        if (top[static_cast<std::size_t>(start)] != 1) {
            continue;
        }
        // A group of top cells, flooded (marked 2).
        std::vector<int> cells;
        stack.push_back(start);
        top[static_cast<std::size_t>(start)] = 2;
        bool edge = false;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            cells.push_back(c);
            const int i = c % side;
            const int j = c / side;
            edge = edge || i == 0 || j == 0 || i == side - 1 || j == side - 1;
            for (const std::array<int, 2> d : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const int ni = i + d[0];
                const int nj = j + d[1];
                if (ni >= 0 && nj >= 0 && ni < side && nj < side &&
                    top[static_cast<std::size_t>(nj * side + ni)] == 1) {
                    top[static_cast<std::size_t>(nj * side + ni)] = 2;
                    stack.push_back(nj * side + ni);
                }
            }
        }
        if (edge || cells.size() > kButteCells) {
            continue;
        }
        Butte b;
        double topHeight = -1e9;
        for (const int c : cells) {
            const double x = x0 + kCell * (c % side - side / 2);
            const double z = z0 + kCell * (c / side - side / 2);
            b.x += x / static_cast<double>(cells.size());
            b.z += z / static_cast<double>(cells.size());
            topHeight = std::max(topHeight, heightAt(x, z));
        }
        // The low ground: the lowest of a ring 1.5 km around.
        double ground = 1e9;
        for (int k = 0; k < 16; ++k) {
            const double a = 2.0 * 3.141592653589793 * k / 16.0;
            ground = std::min(ground, heightAt(b.x + 1500.0 * std::cos(a), b.z + 1500.0 * std::sin(a)));
        }
        b.height = topHeight - ground;
        std::array<double, 8> walls{};
        for (std::size_t k = 0; k < walls.size(); ++k) {
            const double a = 2.0 * 3.141592653589793 * static_cast<double>(k) / 8.0;
            walls[k] = steepestAlong(b.x, b.z, std::cos(a), std::sin(a), 900.0);
        }
        std::sort(walls.begin(), walls.end());
        b.wall = 0.5 * (walls[3] + walls[4]);
        out.push_back(b);
    }
    return out;
}

void testButtes(const Sites& sites) {
    std::vector<Butte> all;
    for (std::size_t e = 0; e < sites.edges.size() && all.size() < 24; ++e) {
        const std::vector<Butte> found = buttesAround(sites.edges[e][0], sites.edges[e][1], 96);
        all.insert(all.end(), found.begin(), found.end());
    }
    int good = 0;
    for (const Butte& b : all) {
        const bool ok = b.height >= kButteLow && b.height <= kButteHigh && b.wall > kSteep60;
        good += ok ? 1 : 0;
        std::printf("butte at (%.0f, %.0f): %.0f m over the low ground, walls %.1f (%.0f degrees)%s\n",
                    b.x, b.z, b.height, b.wall, std::atan(b.wall) * 57.29577951,
                    ok ? "" : " (out of the case)");
    }
    std::printf("buttes: %d of %zu from %zu dry edges, 80 to 400 m with walls over 60 degrees\n",
                good, all.size(), sites.edges.size());
    TEST_CHECK(good >= kLeastButtes);
}

// The deepest floor of a canyon in a box of side cells of kCell meters around (x, z).
std::array<double, 3> deepestFloor(double x0, double z0, int side) {
    std::array<double, 3> best{x0, z0, 0.0};
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const double x = x0 + kCell * (i - side / 2);
            const double z = z0 + kCell * (j - side / 2);
            const Fields f = fieldsAt(x, z);
            if (f.floor >= 1.0 && f.canyon > best[2]) {
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

Section sectionFrom(double x, double z) {
    Section best;
    double rise = -1.0;
    for (int k = 0; k < 24; ++k) {
        const double a = 2.0 * 3.141592653589793 * k / 24.0;
        Section s{std::cos(a), std::sin(a), {}, 0};
        for (double d = 0.0; d <= kSection; d += kSectionStep) {
            s.heights.push_back(heightAt(x + s.dx * d, z + s.dz * d));
            s.rim = s.heights.back() > s.heights[s.rim] ? s.heights.size() - 1 : s.rim;
        }
        // The rise of the rim over the floor, per meter to it: the wall straight up.
        const double r = (s.heights[s.rim] - s.heights[0]) /
                         std::sqrt(static_cast<double>(s.rim) * kSectionStep + 1.0);
        if (r > rise) {
            rise = r;
            best = s;
        }
    }
    return best;
}

// What a section up the wall of a canyon shows.
struct Wall {
    double depth = 0.0;
    int bands = 0;          // of cliffs
    double tallest = 0.0;   // the tallest band in the relief
    double face = 0.0;      // the tallest face of the rock in three dimensions
    double slope500 = 0.0;  // the steepest 500 m
};

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

Wall wallOf(double x, double z, const Section& s) {
    Wall w;
    w.depth = s.heights[s.rim] - s.heights[0];
    // The bands of cliffs up to the rim: runs steeper than kCliffSlope.
    double run = 0.0;
    const auto per500 = static_cast<std::size_t>(500.0 / kSectionStep);
    for (std::size_t i = 1; i <= s.rim + 1; ++i) {
        const double climb = i <= s.rim ? s.heights[i] - s.heights[i - 1] : 0.0;
        if (climb / kSectionStep >= kCliffSlope) {
            run += climb;
        } else {
            w.bands += run >= kLeastBand ? 1 : 0;
            w.tallest = std::max(w.tallest, run);
            run = 0.0;
        }
        if (i >= per500 && i <= s.rim) {
            w.slope500 =
                std::max(w.slope500, std::abs(s.heights[i] - s.heights[i - per500]) / 500.0);
        }
    }
    w.face = tallestFace(x, z, s);
    return w;
}

// The deepest canyons of the thickest plateaus: at least one as the Grand Canyon (1.2 km deep at
// least, 3 bands of cliffs, a face of 120 m).
void testCanyon(const Sites& sites) {
    int good = 0;
    int deep = 0;
    for (std::size_t t = 0; t < sites.thick.size() && t < 6; ++t) {
        const std::array<double, 3> f = deepestFloor(sites.thick[t][0], sites.thick[t][1], 120);
        if (!(f[2] > 0.0)) {
            continue;
        }
        const Section s = sectionFrom(f[0], f[1]);
        const Wall w = wallOf(f[0], f[1], s);
        const bool ok = w.depth >= kCanyonDeep && w.bands >= kLeastBands && w.face >= kTallFace;
        good += ok ? 1 : 0;
        deep += w.depth >= kCanyonDeep ? 1 : 0;
        std::printf("canyon at (%.0f, %.0f), looking (%.2f, %.2f): %.0f m deep (floor %.0f m, rim "
                    "%.0f m %.0f m away), %d bands of cliffs, the tallest %.0f m in the relief, "
                    "the tallest face of the rock %.0f m; the steepest 500 m %.2f (%.0f "
                    "degrees)%s\n",
                    f[0], f[1], s.dx, s.dz, w.depth, s.heights[0], s.heights[s.rim],
                    kSectionStep * static_cast<double>(s.rim), w.bands, w.tallest, w.face,
                    w.slope500, std::atan(w.slope500) * 57.29577951, ok ? " (the case)" : "");
    }
    std::printf("canyons: %d of them 1.2 km deep or more, %d with 3 bands of cliffs and a face of "
                "120 m\n",
                deep, good);
    TEST_CHECK(good >= 1);
}

// The tops: windows of 500 m (11 x 11 heights 50 m apart) on the tops around the sites.
void testTops(const Sites& sites) {
    std::vector<double> spreads;
    const auto onTop = [](double x, double z) {
        const Fields f = fieldsAt(x, z);
        return f.profile >= 0.999 && f.canyon == 0.0 && f.outline > 300.0;
    };
    for (std::size_t t = 0; t < sites.thick.size() && spreads.size() < 120; ++t) {
        for (int k = 0; k < 64 && spreads.size() < 120; ++k) {
            const double x = sites.thick[t][0] + 2'000.0 * (k % 8 - 4);
            const double z = sites.thick[t][1] + 2'000.0 * (k / 8 - 4);
            if (!onTop(x, z) || !onTop(x + 500.0, z + 500.0)) {
                continue;
            }
            double sum = 0.0;
            double squares = 0.0;
            for (int j = 0; j <= 10; ++j) {
                for (int i = 0; i <= 10; ++i) {
                    const double h = heightAt(x + 50.0 * i, z + 50.0 * j);
                    sum += h;
                    squares += h * h;
                }
            }
            const double mean = sum / 121.0;
            spreads.push_back(std::sqrt(std::max(0.0, squares / 121.0 - mean * mean)));
        }
    }
    std::sort(spreads.begin(), spreads.end());
    const auto least = static_cast<std::size_t>(
        std::count_if(spreads.begin(), spreads.end(), [](double v) { return v >= kLeastSpread; }));
    std::printf("tops: %zu windows of 500 m, the standard deviation of their heights %.1f m at the "
                "least, %.1f m the median; %zu over %.1f m\n",
                spreads.size(), spreads.empty() ? 0.0 : spreads.front(),
                spreads.empty() ? 0.0 : spreads[spreads.size() / 2], least, kLeastSpread);
    TEST_CHECK(spreads.size() >= 40);
    TEST_CHECK(static_cast<double>(least) >= kTopsShare * static_cast<double>(spreads.size()));
}

} // namespace

int main() {
    const Sites sites = findSites();
    std::printf("%zu points of the plateaus: tablelandShare differs from at at %zu\n",
                sites.checked, sites.wrong);
    TEST_CHECK(sites.checked > 100 && sites.wrong == 0);
    TEST_CHECK(!sites.edges.empty() && !sites.thick.empty());
    testButtes(sites);
    testCanyon(sites);
    testTops(sites);
    return test::result();
}
