// The shapes of the plateaus (agent 46, tableland_relief.hpp) against real cases, on the default
// world with the plateaus switched on:
//   - buttes beyond the escarpments of the dry plateaus, 80 to 400 m over the low ground around
//     them, their walls steeper than 60 degrees (Monument Valley: Merrick Butte, about 300 m);
//   - their proportions: a vertical cliff 50 to 70 % of their height over a short talus (the De
//     Chelly sandstone over the Organ Rock), the median share over the buttes printed;
//   - the tops never flat (the standard deviation of the heights over 500 m at least 1.5 m).
// Also: the share of the plateaus Tablelands::tablelandShare gives is that of at, bit for bit. The
// canyons: tablelands_canyons_test.
#include "tablelands_fields_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using terrain::tableland_relief::Fields;

// The buttes: 80 to 400 m high, their walls steeper than tan(60 degrees), at least kLeastButtes of
// them; a butte holds at most kButteCells cells of kCell meters of top. Their cliff takes kCliffLow
// to kCliffHigh of their height (the median over them): the fall from their rim steeper than 60
// degrees, over ledges no longer than kLedge meters.
constexpr double kButteLow = 80.0;
constexpr double kButteHigh = 400.0;
constexpr double kSteep60 = 1.7320508;
constexpr int kLeastButtes = 5;
constexpr std::size_t kButteCells = 12;
constexpr double kCell = 250.0;
constexpr double kCliffLow = 0.45;
constexpr double kCliffHigh = 0.75;
constexpr double kLedge = 6.0;
// The tops: their standard deviation over 500 m at least kLeastSpread meters, in kTopsShare of
// the windows at least.
constexpr double kLeastSpread = 1.5;
constexpr double kTopsShare = 0.95;

using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;
using tablelands_fields::Sites;

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
    double wall = 0.0;  // the median of the steepest slopes of its sides
    double cliff = 0.0; // the median share of its height its cliff takes
};

// The share of the height of a butte its cliff takes along the ray from (x, z) in direction
// (dx, dz): from its rim (the first fall steeper than 60 degrees), the fall while steeper, over
// ledges of kLedge meters at most, over its height.
double cliffShareAlong(double x, double z, double dx, double dz, double height) {
    constexpr double kStepMeters = 2.0;
    double previous = heightAt(x, z);
    double fall = 0.0;    // the fall of the cliff
    double pending = 0.0; // that over the current ledge, counted if the cliff goes on under it
    double ledge = 0.0;   // meters of the current ledge
    bool rim = false;
    for (double s = kStepMeters; s <= 1'500.0; s += kStepMeters) {
        const double h = heightAt(x + dx * s, z + dz * s);
        const double drop = previous - h;
        previous = h;
        const bool steep = drop / kStepMeters > kSteep60;
        if (!rim && !steep) {
            continue;
        }
        rim = true;
        if (steep) {
            fall += pending + drop;
            pending = 0.0;
            ledge = 0.0;
            continue;
        }
        ledge += kStepMeters;
        pending += std::max(0.0, drop);
        if (ledge > kLedge) {
            break;
        }
    }
    return fall / height;
}

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

// The median share of the height of a butte its cliff takes, over 8 rays.
double cliffShareOf(const Butte& b) {
    std::array<double, 8> shares{};
    for (std::size_t k = 0; k < shares.size(); ++k) {
        const double a = 2.0 * 3.141592653589793 * static_cast<double>(k) / 8.0;
        shares[k] = cliffShareAlong(b.x, b.z, std::cos(a), std::sin(a), b.height);
    }
    std::sort(shares.begin(), shares.end());
    return 0.5 * (shares[3] + shares[4]);
}

void testButtes(const Sites& sites) {
    std::vector<Butte> all;
    for (std::size_t e = 0; e < sites.edges.size() && all.size() < 48; ++e) {
        const std::vector<Butte> found = buttesAround(sites.edges[e][0], sites.edges[e][1], 96);
        all.insert(all.end(), found.begin(), found.end());
    }
    std::vector<double> cliffs;
    for (Butte& b : all) {
        const bool ok = b.height >= kButteLow && b.height <= kButteHigh && b.wall > kSteep60;
        if (ok) {
            b.cliff = cliffShareOf(b);
            cliffs.push_back(b.cliff);
        }
        std::printf("butte at (%.0f, %.0f): %.0f m over the low ground, walls %.1f (%.0f degrees), "
                    "cliff %.0f %% of its height%s\n",
                    b.x, b.z, b.height, b.wall, std::atan(b.wall) * 57.29577951, 100.0 * b.cliff,
                    ok ? "" : " (out of the case)");
    }
    std::sort(cliffs.begin(), cliffs.end());
    const double median = cliffs.empty() ? 0.0 : cliffs[cliffs.size() / 2];
    std::printf("buttes: %zu of %zu from %zu dry edges, 80 to 400 m with walls over 60 degrees; "
                "their cliff %.0f %% of their height (the median; %.0f to %.0f %%)\n",
                cliffs.size(), all.size(), sites.edges.size(), 100.0 * median,
                cliffs.empty() ? 0.0 : 100.0 * cliffs.front(),
                cliffs.empty() ? 0.0 : 100.0 * cliffs.back());
    TEST_CHECK(static_cast<int>(cliffs.size()) >= kLeastButtes);
    TEST_CHECK(median >= kCliffLow && median <= kCliffHigh);
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
    const Sites sites = tablelands_fields::findSites();
    std::printf("%zu points of the plateaus: tablelandShare differs from at at %zu\n",
                sites.checked, sites.wrong);
    TEST_CHECK(sites.checked > 100 && sites.wrong == 0);
    TEST_CHECK(!sites.edges.empty() && !sites.thick.empty());
    testButtes(sites);
    testTops(sites);
    return test::result();
}
