// The forms of the volcanoes of the default world on its relief (agent 45), measured with and
// without them along rays from their centers: for each form, its rise over the ground it stands
// on, its slopes over 500 m, its crater or caldera and how deep it digs, in the ranges of real
// volcanoes (volcano_draws.cpp); the islands above the sea, the seamounts under it; no perfect
// cone (the outline of the foot); and the nearest volcano of the same form unlike each one
// (rise, radius of the foot, asymmetry).
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"

#include "test_assert.hpp"
#include "volcano_measure_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace vm = volcano_measure;
using terrain::VolcanoForm;

namespace {

constexpr int kPerForm = 6;
constexpr int kPairs = 8;

terrain::HeightfieldParams without() {
    terrain::HeightfieldParams p;
    p.volcanoes.enabled = false;
    return p;
}

// The ranges of a form: rise (meters), the median of the steepest slope within 0.4 of the foot
// and of the slope of its foot (degrees), the depth of its crater (meters); and whether its
// summit stands above the sea or under it (islands, seamounts).
struct Range {
    double riseLow = 0.0;
    double riseHigh = 1e9;
    double upperLow = 0.0;
    double upperHigh = 90.0;
    double footHigh = 90.0;
    double craterLow = 0.0;
    double craterHigh = 1e9;
    double lowestHigh = 1e9; // it digs at least this deep under the ground before it
    int summitSign = 0;      // 1 above the sea, -1 under it
};

Range rangeOf(VolcanoForm f) {
    switch (f) {
    case VolcanoForm::Strato:
        return {1000.0, 5200.0, 14.0, 40.0, 15.0, 20.0};
    case VolcanoForm::TwinStrato: // broader along the axis of its two summits
        return {1000.0, 5200.0, 10.0, 40.0, 15.0, 20.0};
    case VolcanoForm::CollapsedStrato:
        return {1000.0, 5200.0, 12.0, 40.0, 15.0, 100.0};
    case VolcanoForm::Shield:
        return {300.0, 4200.0, 2.0, 16.0, 15.0, 50.0};
    case VolcanoForm::IslandShield:
        return {800.0, 9500.0, 2.0, 18.0, 18.0, 30.0, 1e9, 1e9, 1};
    case VolcanoForm::IslandStrato: // their upper cones as steep as Mayon's (35 to 40 degrees)
        return {500.0, 8000.0, 10.0, 42.0, 18.0, 20.0, 1e9, 1e9, 1};
    case VolcanoForm::Seamount:
        return {200.0, 6000.0, 0.0, 40.0, 20.0, 0.0, 1e9, 1e9, -1};
    case VolcanoForm::Caldera:
        return {300.0, 3500.0, 0.0, 60.0, 15.0, 200.0, 1300.0};
    case VolcanoForm::IslandCaldera:
        return {500.0, 8000.0, 0.0, 60.0, 20.0, 200.0, 1600.0, 1e9, 1};
    case VolcanoForm::PlateauCaldera: // a low plateau: hills around may stand higher
        return {0.0, 1e9, 0.0, 60.0, 10.0, 150.0, 1e9, -50.0};
    case VolcanoForm::MaarField:
        return {0.0, 1e9, 0.0, 90.0, 90.0, 0.0, 1e9, -25.0};
    default:
        return {};
    }
}

bool inRange(const terrain::VolcanoSite& s, const vm::Measure& m) {
    const Range r = rangeOf(s.form);
    const bool edifice = r.upperHigh < 90.0;
    bool ok = m.lowest <= r.lowestHigh;
    if (edifice) {
        ok = ok && m.rise >= r.riseLow && m.rise <= r.riseHigh && m.upper >= r.upperLow &&
             m.upper <= r.upperHigh && m.foot <= r.footHigh && m.crater >= r.craterLow &&
             m.crater <= r.craterHigh;
    }
    if (r.summitSign != 0) {
        ok = ok && (r.summitSign > 0 ? m.summit > 0.0 : m.summit < 0.0);
    }
    return ok;
}

void testForms(const terrain::Heightfield& with, const terrain::Heightfield& bare) {
    std::array<int, terrain::kVolcanoFormCount> done{};
    int failed = 0;
    int measured = 0;
    double outline = 1e9;
    for (const terrain::VolcanoSite& s : with.volcanoes()->sites()) {
        int& n = done[static_cast<std::size_t>(s.form)];
        if (n >= kPerForm) {
            continue;
        }
        ++n;
        const vm::Measure m = vm::measure(with, bare, s);
        const bool ok = inRange(s, m);
        ++measured;
        failed += ok ? 0 : 1;
        if (rangeOf(s.form).upperHigh < 90.0 && s.form != VolcanoForm::Seamount) {
            outline = std::min(outline, m.footMax / std::max(m.footMin, 1.0));
        }
        std::printf("%-16s base %7.0f summit %6.0f rise %5.0f | upper %4.1f foot %4.1f steepest "
                    "%4.1f deg | crater %4.0f | foot %5.0f-%5.0f m | digs %6.1f%s\n",
                    terrain::volcanoFormName(s.form), m.base, m.summit, m.rise, m.upper, m.foot,
                    m.steepest, m.crater, m.footMin, m.footMax, m.lowest, ok ? "" : "  OUT");
    }
    std::printf("%d volcanoes measured, %d out of their ranges; the least uneven foot %.2f\n",
                measured, failed, outline);
    TEST_CHECK(failed == 0);
    // No perfect cone: the foot of every edifice at least 10 % farther along some ray.
    TEST_CHECK(outline > 1.1);
}

// For volcanoes of the edifices, the nearest of the same form: how unlike they are.
void testNeighbors(const terrain::Heightfield& with, const terrain::Heightfield& bare) {
    const std::vector<terrain::VolcanoSite>& sites = with.volcanoes()->sites();
    const terrain::WorldSize& w = with.world();
    std::array<int, terrain::kVolcanoFormCount> done{};
    std::vector<double> unlike;
    int twins = 0;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const terrain::VolcanoSite& a = sites[i];
        int& n = done[static_cast<std::size_t>(a.form)];
        if (n >= kPairs || rangeOf(a.form).upperHigh >= 90.0) {
            continue;
        }
        double best = std::numeric_limits<double>::infinity();
        std::size_t nearest = i;
        for (std::size_t j = 0; j < sites.size(); ++j) {
            if (j == i || sites[j].form != a.form) {
                continue;
            }
            const terrain::VolcanoOffset o = terrain::volcanoOffset(w, a.x, a.z, sites[j].x,
                                                                    sites[j].z);
            const double d = std::hypot(o.dx, o.dz);
            if (d < best) {
                best = d;
                nearest = j;
            }
        }
        if (nearest == i) {
            continue;
        }
        ++n;
        const vm::Measure ma = vm::measure(with, bare, a);
        const vm::Measure mb = vm::measure(with, bare, sites[nearest]);
        const auto rel = [](double x, double y) {
            return std::abs(x - y) / std::max({std::abs(x), std::abs(y), 1.0});
        };
        const double rise = rel(ma.rise, mb.rise);
        const double radius = rel(ma.footMin + ma.footMax, mb.footMin + mb.footMax);
        const double asym = rel(ma.footMax / std::max(ma.footMin, 1.0),
                                mb.footMax / std::max(mb.footMin, 1.0));
        unlike.push_back(std::max({rise, radius, asym}));
        twins += rise < 0.03 && radius < 0.03 && asym < 0.03 ? 1 : 0;
        std::printf("%-16s and its neighbor at %4.0f km: rise %3.0f %%, foot %3.0f %%, "
                    "asymmetry %3.0f %%\n",
                    terrain::volcanoFormName(a.form), best / 1000.0, 100.0 * rise, 100.0 * radius,
                    100.0 * asym);
    }
    const double median = vm::median(unlike);
    std::printf("%zu pairs of neighbors: median of the largest difference %.0f %%, %d alike\n",
                unlike.size(), 100.0 * median, twins);
    TEST_CHECK(unlike.size() >= 40);
    TEST_CHECK(median > 0.15);
    TEST_CHECK(twins == 0);
}

} // namespace

int main() {
    const terrain::Heightfield with(terrain::kDefaultWorldSeed);
    const terrain::Heightfield bare(terrain::kDefaultWorldSeed, without());
    testForms(with, bare);
    testNeighbors(with, bare);
    return test::result();
}
