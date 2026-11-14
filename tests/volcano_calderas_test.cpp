// The walls of the calderas of the default world (agent 45) are never a regular cylinder: around
// each, every quarter of a degree, the height of its crest (the highest ground across its rim),
// the radius where its wall is half way up (its spurs and re-entrants) and the ground at nine
// tenths of its radius (its couloirs and talus). The crest varies by a few percent of the depth at
// least, the radius too; and none of the three repeats at a fixed period: past the first lag where
// its autocorrelation falls below 0, it never climbs back above 0.5. Measured around the caldera
// in its own frame, its ellipse (VolcanoSite::elongation, along axis) stretched back to a circle:
// an elliptical caldera (Crater Lake, 8 by 10 km) is a shape, not a pattern, though its radius
// repeats every half turn.
// Its rim has real low cols and real high points, not only in plan: the height of its crest over
// its floor (the 10th percentile of the ground within half its radius), but across the notch of
// its breach, at
// its highest at least 1.8 times its lowest, its spread at least 12 % of its mean (Crater Lake:
// 150 to 600 m over its lake, 750 to 1,200 m over its floor); and seen from its floor at 1.8 m,
// the elevation of its skyline ranges over 3 degrees at least (1.5 for a caldera in a low
// plateau, 15 to 30 km wide: Yellowstone).
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"

#include "test_assert.hpp"
#include "volcano_on_support.hpp"
#include "volcano_relief.hpp"

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
    const auto stretch = static_cast<double>(s.elongation);
    const double ca = std::cos(static_cast<double>(s.axis));
    const double sa = std::sin(static_cast<double>(s.axis));
    for (int k = 0; k < kAzimuths; ++k) {
        // The azimuth a in the frame of the caldera, back in the world.
        const double a = 2.0 * kPi * k / kAzimuths;
        const double u = std::cos(a) * stretch;
        const double v = std::sin(a);
        const double c = u * ca - v * sa;
        const double sn = u * sa + v * ca;
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

struct Stats {
    double low = 0.0;
    double high = 0.0;
    double mean = 0.0;
    double spread = 0.0;
};

Stats statsOf(const std::vector<double>& v) {
    Stats st;
    st.low = *std::min_element(v.begin(), v.end());
    st.high = *std::max_element(v.begin(), v.end());
    for (const double x : v) {
        st.mean += x;
    }
    st.mean /= static_cast<double>(v.size());
    st.spread = stddev(v);
    return st;
}

// A point at (a radians, r meters) in the frame of the caldera, from its center, in the world.
struct FrameOf {
    double cx = 0.0;
    double cz = 0.0;
    double stretch = 1.0;
    double ca = 1.0;
    double sa = 0.0;
    void at(double a, double r, double& x, double& z) const {
        const double u = r * std::cos(a) * stretch;
        const double v = r * std::sin(a);
        x = cx + u * ca - v * sa;
        z = cz + u * sa + v * ca;
    }
};

FrameOf frameOf(const terrain::VolcanoSite& s) {
    const terrain::VolcanoOffset top = terrain::volcanoSummit(s);
    return {s.x + top.dx, s.z + top.dz, static_cast<double>(s.elongation),
            std::cos(static_cast<double>(s.axis)), std::sin(static_cast<double>(s.axis))};
}

// Its floor: the 10th percentile of 400 points within half its radius; the eye of a walker at its
// center, 1.8 m over its floor.
struct Floor {
    double level = 0.0;
    double eyeX = 0.0;
    double eyeZ = 0.0;
    double eyeHeight = 0.0;
};

Floor floorOf(const terrain::Heightfield& field, const terrain::VolcanoSite& s, const FrameOf& fr) {
    const auto crater = static_cast<double>(s.crater);
    std::vector<double> xs;
    std::vector<double> zs;
    for (int k = 0; k < 400; ++k) {
        double x = 0.0;
        double z = 0.0;
        fr.at(2.39996323 * k, 0.5 * crater * std::sqrt((k + 0.5) / 400.0), x, z);
        xs.push_back(x);
        zs.push_back(z);
    }
    std::vector<float> h(xs.size());
    field.heightsAt(xs, zs, h);
    std::nth_element(h.begin(), h.begin() + 40, h.end());
    Floor f;
    f.level = static_cast<double>(h[40]);
    fr.at(0.0, 0.0, f.eyeX, f.eyeZ);
    f.eyeHeight = f.level + 1.8;
    return f;
}

// The elevation (degrees) of the skyline seen from the eye on the floor, every quarter of a
// degree: the highest over 90 points from 0.5 to 1.45 radii of the caldera (its rim, not the
// cones of its floor).
std::vector<double> skylineOf(const terrain::Heightfield& field, const terrain::VolcanoSite& s,
                              const Floor& f) {
    const double near = 0.5 * static_cast<double>(s.crater);
    const double reach = 1.45 * static_cast<double>(s.crater) * static_cast<double>(s.elongation);
    std::vector<double> out;
    for (int k = 0; k < kAzimuths; ++k) {
        const double a = 2.0 * kPi * k / kAzimuths;
        std::vector<double> xs;
        std::vector<double> zs;
        std::vector<double> ds;
        for (int i = 1; i <= 90; ++i) {
            const double d = near + (reach - near) * i / 90.0;
            xs.push_back(f.eyeX + d * std::cos(a));
            zs.push_back(f.eyeZ + d * std::sin(a));
            ds.push_back(d);
        }
        std::vector<float> h(xs.size());
        field.heightsAt(xs, zs, h);
        double best = -90.0;
        for (std::size_t i = 0; i < h.size(); ++i) {
            best = std::max(best, std::atan((static_cast<double>(h[i]) - f.eyeHeight) / ds[i]) *
                                      180.0 / kPi);
        }
        out.push_back(best);
    }
    return out;
}

// The spill of the basin of the caldera (VolcanoBasin::spill), NaN without one.
double spillOf(const terrain::Heightfield& field, const terrain::VolcanoSite& s,
               std::size_t index) {
    const double r = 1.5 * static_cast<double>(s.crater) * static_cast<double>(s.elongation);
    for (const terrain::VolcanoBasin& b : field.volcanoBasinsIn(s.x - r, s.z - r, s.x + r, s.z + r)) {
        if (!b.maar && b.volcano == index) {
            return static_cast<double>(b.spill);
        }
    }
    return std::nan("");
}

// The lows and highs of the rim seen from the floor, all around but across the notch of its
// breach (3 degrees around it: the col it cuts through counts); false where they are too even.
bool testRelief(const terrain::Heightfield& field, const terrain::VolcanoSite& s,
                std::size_t index, const Walls& w) {
    const FrameOf fr = frameOf(s);
    const Floor f = floorOf(field, s, fr);
    const terrain::volcano_relief::Shape sh = terrain::volcano_relief::shapeOf(s);
    std::vector<double> over;
    for (int k = 0; k < kAzimuths; ++k) {
        const double a = 2.0 * kPi * k / kAzimuths;
        const bool breach =
            sh.breached && std::abs(std::remainder(a - sh.breachAngle, 2.0 * kPi)) < 3.0 * kPi / 180.0;
        if (!breach) {
            over.push_back(w.crest[static_cast<std::size_t>(k)] - f.level);
        }
    }
    const Stats crest = statsOf(over);
    const Stats sky = statsOf(skylineOf(field, s, f));
    const double spill = spillOf(field, s, index);
    const double ratio = crest.high / std::max(crest.low, 1.0);
    // A caldera dug in a low plateau (Yellowstone) is wide and shallow: its rim stands lower over
    // its skyline.
    const double skyRange = s.form == VolcanoForm::PlateauCaldera ? 1.5 : 3.0;
    const bool ok =
        ratio >= 1.8 && crest.spread >= 0.12 * crest.mean && sky.high - sky.low >= skyRange;
    std::printf("    over its floor: crest %4.0f to %4.0f m (x%.2f), mean %4.0f, spread %3.0f m "
                "(%2.0f %%); over its spill %4.0f to %4.0f m; skyline from the floor %4.1f to %4.1f "
                "deg, spread %.1f%s\n",
                crest.low, crest.high, ratio, crest.mean, crest.spread,
                100.0 * crest.spread / crest.mean, crest.low + f.level - spill,
                crest.high + f.level - spill, sky.low, sky.high, sky.spread, ok ? "" : "  EVEN");
    return ok;
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, volcano_test::on());
    int calderas = 0;
    int regular = 0;
    int even = 0;
    int caldera = 0;
    int plateau = 0;
    int island = 0;
    const auto& sites = field.volcanoes()->sites();
    for (std::size_t index = 0; index < sites.size(); ++index) {
        const terrain::VolcanoSite& s = sites[index];
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
        even += testRelief(field, s, index, w) ? 0 : 1;
    }
    std::printf("%d calderas, %d too regular, %d whose rim is too even seen from the floor\n",
                calderas, regular, even);
    TEST_CHECK(calderas >= 8 && regular == 0 && even == 0);
    return test::result();
}
