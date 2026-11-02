// The forms of the dunes (dune_forms.hpp): a transverse ridge with its slip face at 33 degrees,
// a sharp crest, a gentle back and a bare floor; its height capped by its spacing; a barchan, a
// crescent whose horns point downwind, nothing beside it; a seif whose slip face changes side; the
// stars, periodic on the world, none where the lattice holds none.
#include "dune_forms.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

namespace forms = terrain::dune_forms;

constexpr double kDegrees = 57.29577951308232;

// The slope (degrees) of a profile h(t) at t, t in wavelengths of wavelength meters.
template <class F> double slopeOf(F&& h, double t, double wavelength) {
    const double dt = 0.01 / wavelength;
    return std::atan(std::abs(h(t + dt) - h(t)) / 0.01) * kDegrees;
}

void testTransverse() {
    const double wavelength = 2000.0;
    const auto h = [&](double t) { return forms::transverse(t, wavelength, 200.0, 0.1, wavelength); };
    TEST_CHECK(std::abs(h(0.0) - 200.0) < 1e-9);
    // The slip face just downwind of the crest, the back just upwind of it.
    TEST_CHECK(std::abs(slopeOf(h, 0.01, wavelength) - 33.0) < 0.01);
    TEST_CHECK(slopeOf(h, 0.999, wavelength) < 1.0); // the brink: a sharp crest
    double steepestBack = 0.0;
    double floor = 0.0;
    for (double t = 0.2; t < 1.0; t += 0.0005) {
        steepestBack = std::max(steepestBack, slopeOf(h, t, wavelength));
        floor += h(t) == 0.0 ? 0.0005 : 0.0;
    }
    std::printf("transverse: steepest back %.1f deg, bare floor %.0f %% of the wavelength\n",
                steepestBack, 100.0 * floor);
    TEST_CHECK(steepestBack <= 12.5);
    TEST_CHECK(floor > 0.05);
    // Too high for its spacing: capped so that the back stays gentle.
    const double capped = forms::transverse(0.0, 1000.0, 300.0, 0.1, 1000.0);
    TEST_CHECK(capped < 300.0 && capped > 50.0);
    // Never below the floor, never above the height.
    for (double t = 0.0; t < 1.0; t += 0.001) {
        TEST_CHECK(h(t) >= 0.0 && h(t) <= 200.0);
    }
}

void testBarchan() {
    forms::Barchan b;
    b.wavelength = 1200.0;
    b.cell = 900.0;
    b.height = 40.0;
    b.presence = 1.0;
    b.crest = 0.55;
    const auto at = [&](double t, double across) {
        forms::Barchan c = b;
        c.t = t;
        c.across = across;
        return forms::barchan(c);
    };
    // Its crest on its axis; toward the horns, the crest lies farther downwind.
    double axisCrest = 0.0;
    double hornCrest = 0.0;
    double axisTop = 0.0;
    double hornTop = 0.0;
    for (double t = 0.0; t < 1.0; t += 0.0005) {
        if (at(t, 0.5) > axisTop) {
            axisTop = at(t, 0.5);
            axisCrest = t;
        }
        if (at(t, 0.65) > hornTop) {
            hornTop = at(t, 0.65);
            hornCrest = t;
        }
    }
    std::printf("barchan: top %.1f m at %.3f, toward a horn %.1f m at %.3f\n", axisTop, axisCrest,
                hornTop, hornCrest);
    TEST_CHECK(axisTop > 30.0 && axisTop <= 40.0);
    TEST_CHECK(hornCrest > axisCrest && hornTop < axisTop);
    // Nothing beside it, nor where there is none.
    TEST_CHECK(at(axisCrest, 0.1) == 0.0 && at(axisCrest, 0.9) == 0.0);
    forms::Barchan none = b;
    none.presence = 0.0;
    TEST_CHECK(forms::barchan(none) == 0.0);
}

void testSeif() {
    // The slip face on the left at slip 0, on the right at slip 1.
    TEST_CHECK(std::abs(forms::seif(-10.0, 100.0, 0.0) - (100.0 - 10.0 * forms::kLeeTan)) < 1e-9);
    TEST_CHECK(std::abs(forms::seif(10.0, 100.0, 0.0) - (100.0 - 10.0 * forms::kGentleTan)) < 1e-9);
    TEST_CHECK(std::abs(forms::seif(10.0, 100.0, 1.0) - (100.0 - 10.0 * forms::kLeeTan)) < 1e-9);
    TEST_CHECK(forms::seif(1000.0, 100.0, 0.5) == 0.0);
}

void testStars() {
    forms::StarLattice s;
    s.periodX = 32.0 * 65536.0;
    s.periodZ = 16.0 * 65536.0;
    s.height = 200.0;
    s.occupancy = 0.8;
    s.key = 42;
    double highest = 0.0;
    bool periodic = true;
    for (int k = 0; k < 20'000; ++k) {
        const double x = -0.5 * s.periodX + 37.3 * k;
        const double z = 11.1 * k - 0.5 * s.periodZ;
        const double h = forms::stars(s, x, z);
        highest = std::max(highest, h);
        // One period away, the same stars.
        periodic = periodic && std::abs(forms::stars(s, x + s.periodX, z) - h) < 1e-6;
    }
    TEST_CHECK(periodic);
    TEST_CHECK(highest > 100.0 && highest <= 1.2 * 200.0);
    forms::StarLattice empty = s;
    empty.occupancy = 0.0;
    TEST_CHECK(forms::stars(empty, 100.0, 100.0) == 0.0);
}

} // namespace

int main() {
    testTransverse();
    testBarchan();
    testSeif();
    testStars();
    return test::result();
}
