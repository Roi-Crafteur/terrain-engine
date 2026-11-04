// The walls of the calderas of the default world (agent 45) are never a regular cylinder: around
// each, every quarter of a degree, the height of its crest (the highest ground across its rim),
// the radius where its wall is half way up (its spurs and re-entrants) and the ground at nine
// tenths of its radius (its couloirs and talus). The crest varies by a few percent of the depth at
// least, the radius too; and none of the three repeats at a fixed period: past the first lag where
// its autocorrelation falls below 0, it never climbs back above 0.5.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using terrain::VolcanoForm;

constexpr int kAzimuths = 1440;
constexpr double kPi = 3.14159265358979323846;

double stddev(const std::vector<double>& v) {
    double mean = 0.0;
    for (const double x : v) {
        mean += x;
    }
    mean /= static_cast<double>(v.size());
    double sum = 0.0;
    for (const double x : v) {
        sum += (x - mean) * (x - mean);
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

// The highest autocorrelation of the series around the circle beyond its first lag below 0: high
// for a pattern of fixed period, low for one that never repeats.
double periodicity(const std::vector<double>& v) {
    const std::size_t n = v.size();
    double mean = 0.0;
    for (const double x : v) {
        mean += x;
    }
    mean /= static_cast<double>(n);
    const auto at = [&](std::size_t lag) {
        double sum = 0.0;
        double norm = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            sum += (v[i] - mean) * (v[(i + lag) % n] - mean);
            norm += (v[i] - mean) * (v[i] - mean);
        }
        return norm > 0.0 ? sum / norm : 0.0;
    };
    double highest = -1.0;
    bool crossed = false;
    for (std::size_t lag = 1; lag <= n / 2; ++lag) {
        const double r = at(lag);
        crossed = crossed || r < 0.0;
        if (crossed) {
            highest = std::max(highest, r);
        }
    }
    return highest;
}

struct Walls {
    std::vector<double> crest;  // meters
    std::vector<double> middle; // meters from the center
    std::vector<double> ring;   // the ground at 0.9 of the radius
};

Walls wallsOf(const terrain::Heightfield& field, const terrain::VolcanoSite& s) {
    const terrain::VolcanoOffset top = terrain::volcanoSummit(s);
    const double cx = s.x + top.dx;
    const double cz = s.z + top.dz;
    const auto crater = static_cast<double>(s.crater);
    Walls w;
    for (int k = 0; k < kAzimuths; ++k) {
        const double a = 2.0 * kPi * k / kAzimuths;
        const double c = std::cos(a);
        const double sn = std::sin(a);
        std::vector<double> xs;
        std::vector<double> zs;
        for (int i = 0; i <= 60; ++i) {
            const double r = crater * (0.5 + 0.8 * i / 60.0);
            xs.push_back(cx + r * c);
            zs.push_back(cz + r * sn);
        }
        std::vector<float> h(xs.size());
        field.heightsAt(xs, zs, h);
        const float crest = *std::max_element(h.begin(), h.end());
        const float floor = *std::min_element(h.begin(), h.begin() + 10);
        double middle = crater * 1.3;
        for (std::size_t i = 0; i < h.size(); ++i) {
            if (h[i] >= floor + 0.5f * (crest - floor)) {
                middle = crater * (0.5 + 0.8 * static_cast<double>(i) / 60.0);
                break;
            }
        }
        w.crest.push_back(crest);
        w.middle.push_back(middle);
        w.ring.push_back(field.heightAt(cx + 0.9 * crater * c, cz + 0.9 * crater * sn));
    }
    return w;
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    int calderas = 0;
    int regular = 0;
    int caldera = 0;
    int plateau = 0;
    int island = 0;
    for (const terrain::VolcanoSite& s : field.volcanoes()->sites()) {
        int* count = s.form == VolcanoForm::Caldera          ? &caldera
                     : s.form == VolcanoForm::PlateauCaldera ? &plateau
                     : s.form == VolcanoForm::IslandCaldera  ? &island
                                                             : nullptr;
        if (count == nullptr || *count >= 3) {
            continue;
        }
        ++*count;
        ++calderas;
        const Walls w = wallsOf(field, s);
        const auto depth = static_cast<double>(s.craterDepth);
        const double crest = stddev(w.crest) / depth;
        const double radius = stddev(w.middle) / static_cast<double>(s.crater);
        const double pc = periodicity(w.crest);
        const double pm = periodicity(w.middle);
        const double pr = periodicity(w.ring);
        const bool ok = crest >= 0.05 && radius >= 0.02 && pc < 0.5 && pm < 0.5 && pr < 0.5;
        regular += ok ? 0 : 1;
        std::printf("%-15s %5.0f m deep, %5.0f m wide: crest varies %4.1f %% of the depth, the "
                    "radius of the wall %4.1f %%; periodicity crest %5.2f, wall %5.2f, ring "
                    "%5.2f%s\n",
                    terrain::volcanoFormName(s.form), depth, 2.0 * static_cast<double>(s.crater),
                    100.0 * crest, 100.0 * radius, pc, pm, pr, ok ? "" : "  REGULAR");
    }
    std::printf("%d calderas, %d too regular\n", calderas, regular);
    TEST_CHECK(calderas >= 8 && regular == 0);
    return test::result();
}
