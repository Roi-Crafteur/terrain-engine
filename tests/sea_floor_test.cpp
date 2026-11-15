// The profiles of the floor of the seas (agent 51, sea_floor.hpp): the continental margins (shelf,
// break, slope, foot, rise; an active one without rise), the depth of the crust by its age (GDH1),
// the axes of the slow and fast ridges, the rounded corners.

#include "sea_floor.hpp"

#include "terrain/det_math.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdio>

namespace {

namespace sf = terrain::sea_floor;
constexpr double kDegrees = 57.29577951308232;

double gradientAt(const sf::Margin& m, double d) {
    return (sf::marginAt(m, d - 500.0) - sf::marginAt(m, d + 500.0)) / 1'000.0;
}

void testPassiveMargin() {
    sf::Margin m; // 80 km of shelf down to 130 m, slope 0.07 to 2,500 m, rise 0.01
    TEST_CHECK(std::abs(sf::marginAt(m, 0.0)) < 1.0);
    TEST_CHECK(std::abs(sf::marginAt(m, m.shelfWidth) + m.breakDepth) < m.corner);
    double last = 1.0;
    for (double d = 0.0; d <= 600'000.0; d += 1'000.0) {
        const double h = sf::marginAt(m, d);
        TEST_CHECK(h < last); // ever deeper out to sea
        last = h;
    }
    const double foot = m.shelfWidth + (m.footDepth - m.breakDepth) / m.slopeGradient;
    const double shelf = gradientAt(m, 0.5 * m.shelfWidth);
    const double slope = gradientAt(m, 0.5 * (m.shelfWidth + foot));
    const double rise = gradientAt(m, foot + 100'000.0);
    std::printf("passive margin: shelf %.4f, slope %.2f deg, rise %.2f deg, foot at %.0f km\n",
                shelf, terrain::dmath::atan(slope) * kDegrees,
                terrain::dmath::atan(rise) * kDegrees, foot * 1e-3);
    TEST_CHECK(std::abs(shelf - m.breakDepth / m.shelfWidth) < 1e-4);
    TEST_CHECK(std::abs(slope - m.slopeGradient) < 1e-3);
    TEST_CHECK(std::abs(rise - m.riseGradient) < 1e-3);
}

void testActiveMargin() {
    sf::Margin m;
    m.shelfWidth = 20'000.0;
    m.slopeGradient = 0.1;
    m.riseShare = 0.0; // no rise
    const double far = sf::marginAt(m, 120'000.0);
    TEST_CHECK(std::abs(far - (-m.breakDepth - m.slopeGradient * 100'000.0)) < 1.0);
    TEST_CHECK(std::abs(gradientAt(m, 60'000.0) - 0.1) < 1e-4);
}

void testAges() {
    TEST_CHECK(sf::depthOfAge(0.0) == 2'500.0);
    TEST_CHECK(sf::depthOfAge(-5.0) == 2'500.0);
    std::printf("Parsons and Sclater: %.1f m at 26 Myr, %.1f m at 130, %.1f m at 200, %.1f m at "
                "1000\n",
                sf::depthOfAge(26.0), sf::depthOfAge(130.0), sf::depthOfAge(200.0),
                sf::depthOfAge(1'000.0));
    // Ever deeper, never 6,400 m, and continuous where the two pieces cross (near 26 Myr): no
    // step larger than the slope of either over a tenth of a million years.
    double last = 0.0;
    for (double age = 0.0; age < 400.0; age += 0.1) {
        const double d = sf::depthOfAge(age);
        TEST_CHECK(d > last && d < 6'400.0);
        TEST_CHECK(age < 4.0 || d - last < 10.0);
        last = d;
    }
    TEST_CHECK(std::abs(sf::depthOfAge(130.0) - 6'000.0) < 30.0);
    TEST_CHECK(std::abs(sf::depthOfAge(200.0) - 6'267.0) < 3.0);
    TEST_CHECK(std::abs(sf::depthOfAge(16.0) - 3'900.0) < 1e-9); // the young piece
}

void testAxes() {
    const sf::Axis a;
    TEST_CHECK(std::abs(sf::axisRelief(a, 0.0, 0.0) + a.valleyDepth) < 1e-9);
    TEST_CHECK(std::abs(sf::axisRelief(a, 3'000.0, 0.0) + a.valleyDepth) < 1e-9); // its floor
    TEST_CHECK(sf::axisRelief(a, a.valleyWidth + 1.0, 0.0) == 0.0);
    TEST_CHECK(std::abs(sf::axisRelief(a, 0.0, 1.0) - a.highHeight) < 1e-9);
    TEST_CHECK(sf::axisRelief(a, 0.0, 0.5) == 0.0); // neither slow nor fast
    TEST_CHECK(sf::axisRelief(a, -8'000.0, 0.1) == sf::axisRelief(a, 8'000.0, 0.1));
}

void testCorners() {
    TEST_CHECK(std::abs(sf::smoothMax(-100.0, -5'000.0, 150.0) + 100.0) < 2.0);
    TEST_CHECK(std::abs(sf::smoothMin(-100.0, -5'000.0, 150.0) + 5'000.0) < 2.0);
    TEST_CHECK(sf::smoothMax(3.0, 7.0, 0.0) == 7.0 && sf::smoothMin(3.0, 7.0, 0.0) == 3.0);
    TEST_CHECK(sf::step(10.0, 0.0, 0.0) == 1.0 && sf::step(10.0, 0.0, 10.0) == 0.0);
}

} // namespace

int main() {
    testPassiveMargin();
    testActiveMargin();
    testAges();
    testAxes();
    testCorners();
    return test::result();
}
