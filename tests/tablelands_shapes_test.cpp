// The shapes of the plateaus (agent 46, tableland_relief.hpp) against real cases, on the default
// world with the plateaus switched on:
//   - buttes beyond the escarpments of the dry plateaus, 80 to 400 m over the low ground around
//     them, their walls steeper than 60 degrees (Monument Valley: Merrick Butte, about 300 m);
//   - their proportions: a vertical cliff 50 to 70 % of their height over a short talus (the De
//     Chelly sandstone over the Organ Rock), the median share over the buttes printed;
//   - the tops never flat (the standard deviation of the heights over 500 m at least 1.5 m).
// Also: the share of the plateaus Tablelands::tablelandShare gives is that of at, bit for bit. The
// canyons: tablelands_canyons_test.
#include "tablelands_buttes_support.hpp"
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
// them (tablelands_buttes_support.hpp). Their cliff takes kCliffLow to kCliffHigh of their own fall
// (the median over them, cliffShareAlong).
constexpr double kButteLow = 80.0;
constexpr double kButteHigh = 400.0;
constexpr int kLeastButtes = 5;
constexpr double kCliffLow = 0.45;
constexpr double kCliffHigh = 0.75;
// The tops: their standard deviation over 500 m at least kLeastSpread meters, in kTopsShare of
// the windows at least.
constexpr double kLeastSpread = 1.5;
constexpr double kTopsShare = 0.95;

using tablelands_buttes::Butte;
using tablelands_buttes::buttesAround;
using tablelands_buttes::cliffShareOf;
using tablelands_buttes::kSteep60;
using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;
using tablelands_fields::Sites;

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
