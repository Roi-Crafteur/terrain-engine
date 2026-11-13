// The impact craters of agent 52 (impact_craters.hpp, crater_forms.hpp):
// - their forms: a bowl or a flat floor under the ground around, a rim over it, a central peak
//   from 4 km across, a ring of peaks instead from 40 km, terraces from 6 km; continuous; nothing
//   (not a bit) from 2.5 radii; never round; worn, the rim lower, the floor filled;
// - their wear grows with their age, the humidity of their climate and the softness of their rock;
// - on the default world, the lows on: about 200, 80 and 15 of them in the three classes, most of
//   them worn, the fresh ones in climates drier by 15 % at least than the worn-out ones (its
//   pre-climate, P / (P + PET)); never in
//   a cluster (their reaches 20 km apart, apart from every other low); their whole footprint on
//   the plains far from the sea; their floor under the ground around, their rim over it.
#include "crater_forms.hpp"
#include "impact_craters.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/pre_climate.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using terrain::LowKind;
using terrain::LowSite;
namespace lows = terrain::lows;
namespace dmath = terrain::dmath;

lows::Crater make(double diameter, double wear) {
    lows::Crater c = lows::freshCrater(diameter, 0.7);
    c.harmonics = {0.04, 0.02, 0.03, 0.2};
    c.phases = {0.1, 0.3, 0.6, 0.85};
    c.wear = wear;
    c.reach = lows::craterReach(c);
    return c;
}

// The crater at rho times the radius of its crest, toward turns.
lows::CraterShape along(const lows::Crater& c, double turns, double rho) {
    const double r = rho * lows::crestRadius(c, turns);
    return lows::shapeAt(c, r * dmath::cosTurns(turns), r * dmath::sinTurns(turns));
}

void testForm(double diameter) {
    const lows::Crater fresh = make(diameter, 0.0);
    const lows::Crater worn = make(diameter, 0.9);
    TEST_CHECK(lows::craterFloor(fresh) < 0.0 && lows::craterFloor(worn) < 0.0);
    TEST_CHECK(lows::craterFloor(worn) > lows::craterFloor(fresh));
    const double onFloor = fresh.floorShare < 0.35 ? 0.1 : 0.3;
    for (int k = 0; k < 8; ++k) {
        const double t = k / 8.0;
        TEST_CHECK(along(fresh, t, 1.0).delta > 0.0);
        TEST_CHECK(along(worn, t, 1.0).delta < along(fresh, t, 1.0).delta);
        TEST_CHECK(std::abs(along(fresh, t, onFloor).delta - lows::craterFloor(fresh)) < 1e-6);
        TEST_CHECK(along(fresh, t, 2.5).delta == 0.0 && along(fresh, t, 2.5).inside == 0.0);
    }
    const lows::CraterShape beyond = lows::shapeAt(fresh, fresh.reach, 0.0);
    TEST_CHECK(beyond.delta == 0.0 && beyond.inside == 0.0 && beyond.flat == 0.0);
    // The peak, the ring, the terraces.
    const double rise = lows::shapeAt(fresh, 0.0, 0.0).delta - lows::craterFloor(fresh);
    const double ring = along(fresh, 0.25, 0.45).delta - lows::craterFloor(fresh);
    if (diameter < 4'000.0) {
        TEST_CHECK(rise == 0.0 && fresh.terraces == 0);
    } else if (diameter >= 8'000.0 && diameter < 40'000.0) {
        TEST_CHECK(rise >= 0.3 * fresh.depth);
    } else if (diameter >= 55'000.0) {
        TEST_CHECK(rise < 0.05 * fresh.depth && ring >= 0.2 * fresh.depth);
    }
    TEST_CHECK(diameter < 6'000.0 || fresh.terraces >= 2);
    // Continuous: no step over 3 m between points 1 m apart, from the center out.
    double worst = 0.0;
    double previous = lows::shapeAt(fresh, 0.0, 0.0).delta;
    for (double d = 1.0; d < std::min(fresh.reach, 20'000.0); d += 1.0) {
        const double h = lows::shapeAt(fresh, 0.6 * d, 0.8 * d).delta;
        worst = std::max(worst, std::abs(h - previous));
        previous = h;
    }
    TEST_CHECK(worst <= 3.0);
    // Never round.
    double low = 1e30;
    double high = 0.0;
    for (int k = 0; k < 72; ++k) {
        low = std::min(low, lows::crestRadius(fresh, k / 72.0));
        high = std::max(high, lows::crestRadius(fresh, k / 72.0));
    }
    TEST_CHECK(high >= 1.03 * low);
    std::printf("%6.0f m across: depth %4.0f m, rim %3.0f m, floor %5.0f m (worn %4.0f m), "
                "center %4.0f m and 0.45 radius %4.0f m over the floor, %d terraces, steepest "
                "step %.2f m\n",
                diameter, fresh.depth, fresh.rim, lows::craterFloor(fresh),
                lows::craterFloor(worn), rise, ring, fresh.terraces, worst);
}

void testWear() {
    TEST_CHECK(lows::craterWear(0.0, 0.5f, 0.5f) == 0.0);
    for (double age = 0.1; age < 1.0; age += 0.2) {
        const double w = lows::craterWear(age, 0.3f, 0.5f);
        TEST_CHECK(w > 0.0 && w < 1.0);
        TEST_CHECK(lows::craterWear(age, 0.6f, 0.5f) > w);           // wetter
        TEST_CHECK(lows::craterWear(age, 0.3f, 0.1f) > w);           // softer
        TEST_CHECK(lows::craterWear(age + 0.05, 0.3f, 0.5f) > w);    // older
    }
}

const terrain::Heightfield& field() {
    static const terrain::Heightfield f = [] {
        terrain::HeightfieldParams p;
        p.lows.enabled = true;
        return terrain::Heightfield(terrain::kDefaultWorldSeed, p);
    }();
    return f;
}

double wrapped(double d, double period) {
    return d - period * std::floor(d / period + 0.5);
}

// The mean ground on a ring of rho radii around a crater, 16 points.
double ringMean(const LowSite& c, double rho) {
    double sum = 0.0;
    for (int k = 0; k < 16; ++k) {
        sum += field().heightAt(c.x + rho * c.radius * dmath::cosTurns(k / 16.0),
                                c.z + rho * c.radius * dmath::sinTurns(k / 16.0));
    }
    return sum / 16.0;
}

// The mean over 8 directions of the highest ground from 0.8 to 1.3 radii.
double rimMean(const LowSite& c) {
    double sum = 0.0;
    for (int k = 0; k < 8; ++k) {
        float top = -1e9f;
        for (double rho = 0.8; rho <= 1.3; rho += 0.02) {
            top = std::max(top, field().heightAt(c.x + rho * c.radius * dmath::cosTurns(k / 8.0),
                                                 c.z + rho * c.radius * dmath::sinTurns(k / 8.0)));
        }
        sum += static_cast<double>(top);
    }
    return sum / 8.0;
}

bool onPlains(const LowSite& c) {
    const terrain::Geography& g = *field().geography();
    bool plain = true;
    for (int ring = 0; ring <= 4; ++ring) {
        for (int k = 0; k < 16; ++k) {
            const double r = ring * c.reach / 4.0;
            const terrain::GeographySample p = g.sampleAt(c.x + r * dmath::cosTurns(k / 16.0),
                                                          c.z + r * dmath::sinTurns(k / 16.0));
            plain = plain && p.relief <= 0.12f && p.coast >= 30'000.0 && p.belt <= 0.0f;
        }
    }
    return plain;
}

void testWorld() {
    const terrain::WorldSize& w = field().world();
    const std::vector<LowSite> all = field().lowsIn(-0.5 * w.x, -0.5 * w.z, 0.5 * w.x, 0.5 * w.z);
    std::vector<LowSite> craters;
    std::copy_if(all.begin(), all.end(), std::back_inserter(craters),
                 [](const LowSite& s) { return s.kind == LowKind::Crater; });
    std::array<int, lows::kCraterClasses> counts{};
    std::size_t worn = 0;
    for (const LowSite& c : craters) {
        ++counts[static_cast<std::size_t>(lows::craterClass(2.0 * c.radius))];
        worn += c.wear >= 1.0f / 3.0f ? 1 : 0;
    }
    std::printf("craters: %d, %d, %d; %zu of %zu worn\n", counts[0], counts[1], counts[2], worn,
                craters.size());
    TEST_CHECK(counts[0] >= 180 && counts[1] >= 72 && counts[2] >= 13);
    TEST_CHECK(worn >= 6 * craters.size() / 10 && worn < craters.size());
    // No cluster: 20 km between their reaches and those of every other low.
    std::size_t close = 0;
    for (const LowSite& c : craters) {
        for (const LowSite& o : all) {
            const double dx = wrapped(c.x - o.x, w.x);
            const double dz = wrapped(c.z - o.z, w.z);
            const double gap = c.reach + o.reach + 20'000.0;
            close += !(c == o) && dx * dx + dz * dz < gap * gap ? 1 : 0;
        }
    }
    TEST_CHECK(close == 0);
    // The fresh ones in drier climates than the worn-out ones.
    const terrain::PreClimate climate(*field().geography());
    double fresh = 0.0;
    double out = 0.0;
    std::size_t freshCount = 0;
    std::size_t outCount = 0;
    for (const LowSite& c : craters) {
        const double h = static_cast<double>(climate.humidityAt(c.x, c.z));
        fresh += c.wear < 1.0f / 3.0f ? h : 0.0;
        freshCount += c.wear < 1.0f / 3.0f ? 1 : 0;
        out += c.wear >= 2.0f / 3.0f ? h : 0.0;
        outCount += c.wear >= 2.0f / 3.0f ? 1 : 0;
    }
    std::printf("mean humidity: fresh %.3f (%zu), worn out %.3f (%zu)\n",
                fresh / static_cast<double>(freshCount), freshCount,
                out / static_cast<double>(outCount), outCount);
    TEST_CHECK(freshCount > 0 && outCount > 0);
    TEST_CHECK(fresh / static_cast<double>(freshCount) <
               0.85 * out / static_cast<double>(outCount));
}

// Their floor under the ground around, their rim over it (the fresh ones), on the plains.
void testGround() {
    const terrain::WorldSize& w = field().world();
    std::size_t deep = 0;
    std::size_t lower = 0;
    std::size_t fresh = 0;
    std::size_t raised = 0;
    std::size_t plains = 0;
    std::size_t count = 0;
    for (const LowSite& c : field().lowsIn(-0.5 * w.x, -0.5 * w.z, 0.5 * w.x, 0.5 * w.z)) {
        if (c.kind != LowKind::Crater) {
            continue;
        }
        ++count;
        plains += onPlains(c) ? 1 : 0;
        TEST_CHECK(field().mayBeDryBelowSea(c.x, c.z));
        const double outside = ringMean(c, 2.7);
        if (c.floor <= -50.0f && c.radius >= 1'000.0) {
            ++deep;
            lower += ringMean(c, 0.3) < outside - 0.5 * static_cast<double>(-c.floor) ? 1 : 0;
        }
        if (c.wear < 1.0f / 3.0f && c.radius >= 1'000.0) {
            ++fresh;
            raised += rimMean(c) > outside ? 1 : 0;
        }
    }
    std::printf("on the plains %zu of %zu; floor lower %zu of %zu; rim raised %zu of %zu\n",
                plains, count, lower, deep, raised, fresh);
    TEST_CHECK(plains == count);
    TEST_CHECK(deep > 10 && lower == deep);
    TEST_CHECK(fresh > 3 && raised == fresh);
}

} // namespace

int main() {
    for (const double d : {500.0, 1'200.0, 3'000.0, 5'000.0, 12'000.0, 30'000.0, 50'000.0,
                           60'000.0}) {
        testForm(d);
    }
    testWear();
    testWorld();
    testGround();
    return test::result();
}
