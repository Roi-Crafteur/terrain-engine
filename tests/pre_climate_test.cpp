// The pre-climate (pre_climate.hpp, shared by agents 46 and 52) against the climate of the planet
// level of the default world (its planet file, the fixture coast_planet): their humidities over the
// interior of the land correlate, their median gap is small and they agree on dry or humid; the
// karst (tablelands.hpp) lies on land the planet level makes humid enough, the mesas of the dry lands
// on land it makes dry. Prints where they err most (karst on a dry land, mesas on a humid one).
#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/pre_climate.hpp"
#include "terrain/generation/tablelands.hpp"
#include "terrain/map/map_tablelands.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// The points: a lattice kStep meters apart, on the land more than kInterior meters from the coast
// of the geography (the coastline of the coarse grid of the pre-climate is 65 km wide).
constexpr double kStep = 32'768.0;
constexpr double kInterior = 200'000.0;
// The humidity splitting the dry lands from the humid ones (PlanetLevel::humidityAt).
constexpr float kSplit = 0.4f;
// The bounds: the correlation, the median gap, the share of the points of one class.
constexpr double kLeastCorrelation = 0.85;
constexpr double kMostMedianGap = 0.06;
constexpr double kLeastAgreement = 0.85;
// The karst on land at least kKarstDryHumidity humid, at least kKarstAgree of it; the mesas of the
// dry lands at most kMesaHumidHumidity, at least kMesaAgree of them (map_tablelands.hpp).
constexpr double kKarstAgree = 0.9;
constexpr double kMesaAgree = 0.85;
constexpr float kFull = 0.5f;
constexpr std::size_t kWorst = 10;

struct Point {
    double x = 0.0;
    double z = 0.0;
    float pre = 0.0f;
    float planet = 0.0f;
    terrain::TablelandPoint at;
};

std::vector<Point> points(const terrain::Heightfield& field, const terrain::PlanetLevel& planet) {
    std::vector<Point> out;
    const terrain::WorldSize& w = field.world();
    const terrain::PreClimate& pre = field.tablelands()->climate();
    for (double z = -0.5 * w.z + 0.5 * kStep; z < 0.5 * w.z; z += kStep) {
        for (double x = -0.5 * w.x + 0.5 * kStep; x < 0.5 * w.x; x += kStep) {
            const terrain::GeographySample g = field.geography()->sampleAt(x, z);
            if (!(g.coast > kInterior) || planet.sampleAt(x, z).ocean) {
                continue;
            }
            out.push_back({x, z, pre.humidityAt(x, z), planet.humidityAt(x, z),
                           field.tablelands()->at(x, z, g)});
        }
    }
    return out;
}

void testAgreement(const std::vector<Point>& ps) {
    double sa = 0.0;
    double sb = 0.0;
    for (const Point& p : ps) {
        sa += p.pre;
        sb += p.planet;
    }
    const double n = static_cast<double>(ps.size());
    const double ma = sa / n;
    const double mb = sb / n;
    double cov = 0.0;
    double va = 0.0;
    double vb = 0.0;
    std::size_t agree = 0;
    std::vector<double> gaps;
    for (const Point& p : ps) {
        cov += (p.pre - ma) * (p.planet - mb);
        va += (p.pre - ma) * (p.pre - ma);
        vb += (p.planet - mb) * (p.planet - mb);
        agree += (p.pre < kSplit) == (p.planet < kSplit) ? 1 : 0;
        gaps.push_back(std::abs(static_cast<double>(p.pre) - p.planet));
    }
    std::sort(gaps.begin(), gaps.end());
    const double r = cov / std::sqrt(va * vb);
    const double median = gaps[gaps.size() / 2];
    const double share = static_cast<double>(agree) / n;
    std::printf("%zu points of the interior: correlation %.3f, median gap %.3f (90th %.3f), "
                "%.1f %% agree on dry or humid (split %.2f), mean %.3f / %.3f\n",
                ps.size(), r, median, gaps[gaps.size() * 9 / 10], 100.0 * share, kSplit, ma, mb);
    TEST_CHECK(r >= kLeastCorrelation);
    TEST_CHECK(median <= kMostMedianGap);
    TEST_CHECK(share >= kLeastAgreement);
}

// The points of a province and how many of them err, the worst first.
void reportErrors(const char* what, std::vector<std::pair<double, const Point*>> errors,
                  std::size_t count, double least) {
    std::sort(errors.begin(), errors.end(), [](const auto& a, const auto& b) {
        return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    const double ok = count == 0 ? 1.0
                                 : 1.0 - static_cast<double>(errors.size()) /
                                             static_cast<double>(count);
    std::printf("%s: %zu points, %zu on the wrong climate of the planet level (%.1f %% right)\n",
                what, count, errors.size(), 100.0 * ok);
    for (std::size_t k = 0; k < std::min(kWorst, errors.size()); ++k) {
        const Point& p = *errors[k].second;
        std::printf("  worst %zu: x %.0f z %.0f, humidity %.3f (pre-climate %.3f), share %.2f\n",
                    k + 1, p.x, p.z, p.planet, p.pre, std::max(p.at.karst, p.at.tableland));
    }
    TEST_CHECK(count > 0);
    TEST_CHECK(ok >= least);
}

void testProvinces(const std::vector<Point>& ps) {
    std::vector<std::pair<double, const Point*>> karst;
    std::vector<std::pair<double, const Point*>> mesas;
    std::size_t karstCount = 0;
    std::size_t mesaCount = 0;
    for (const Point& p : ps) {
        if (p.at.karst > kFull) {
            ++karstCount;
            if (p.planet < terrain::kKarstDryHumidity) {
                karst.emplace_back(terrain::kKarstDryHumidity - p.planet, &p);
            }
        }
        if (p.at.tableland > kFull && p.at.arid > kFull) {
            ++mesaCount;
            if (p.planet > terrain::kMesaHumidHumidity) {
                mesas.emplace_back(p.planet - terrain::kMesaHumidHumidity, &p);
            }
        }
    }
    reportErrors("karst on dry land", karst, karstCount, kKarstAgree);
    reportErrors("mesas of the dry lands on humid land", mesas, mesaCount, kMesaAgree);
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const terrain::PlanetLevel planet(terrain::loadPlanetFile(coast_planet::filePath(),
                                                              terrain::kDefaultWorldSeed,
                                                              terrain::PlanetParams{},
                                                              terrain::WorldSize{}));
    const std::vector<Point> ps = points(field, planet);
    testAgreement(ps);
    testProvinces(ps);
    return test::result();
}
