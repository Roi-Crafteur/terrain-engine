// The breaches of the calderas of the default world (agent 45): a gorge cut down the flank, never
// a straight notch. From its lip (the lowest ground across the rim at its azimuth) its thalweg is
// followed down, every 40 m, by the lowest ground across its course near the last point; at each
// step its cross section: its depth under the lower of its two banks, its width at half that
// depth, the slopes of its two walls there. Each gorge on land winds (its thalweg at least 1.1
// times as long as the line between its ends), is at least 1.8 times as wide 0.7 of the way
// down as at its lip and 1.2 times as deep 0.4 of the way down, its walls uneven, its floor never
// rising on the way down (no pond); the lake of its caldera stays under its lip. A strait into a
// drowned caldera keeps its floor 10 m under the sea at least, out to the sea (or opens in a gap
// of its ring of islands already under the sea).
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"

#include "test_assert.hpp"
#include "volcano_on_support.hpp"
#include "volcano_relief.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using terrain::VolcanoForm;
using terrain::VolcanoSite;

constexpr double kPi = 3.14159265358979323846;
constexpr double kStep = 40.0;
constexpr double kCrossStep = 10.0;
constexpr double kCross = 1500.0; // half the length of a cross section

struct Section {
    double x = 0.0;
    double z = 0.0;
    double floor = 0.0;
    double depth = 0.0;      // under the lower of its two edges
    double width = 0.0;      // between its edges
    double slopeLeft = 0.0;  // the mean slope of each wall, from the floor to its edge
    double slopeRight = 0.0;
};

// The cross section through (x, z) across the direction (dx, dz): the lowest point within search
// meters of it (the middle of a flat floor), then the edges of its walls on each side.
Section sectionAt(const terrain::Heightfield& field, double x, double z, double dx, double dz,
                  double search) {
    const double nx = -dz;
    const double nz = dx;
    const int half = static_cast<int>(kCross / kCrossStep);
    std::vector<double> xs;
    std::vector<double> zs;
    for (int i = -half; i <= half; ++i) {
        xs.push_back(x + nx * i * kCrossStep);
        zs.push_back(z + nz * i * kCrossStep);
    }
    std::vector<float> h(xs.size());
    field.heightsAt(xs, zs, h);
    const int reach = std::max(1, static_cast<int>(search / kCrossStep));
    int low = half;
    for (int i = half - reach; i <= half + reach; ++i) {
        low = h[static_cast<std::size_t>(i)] < h[static_cast<std::size_t>(low)] ? i : low;
    }
    const auto at = [&](int i) { return static_cast<double>(h[static_cast<std::size_t>(i)]); };
    // The middle of the floor (within 1 m of its lowest point): a flat floor has no lowest point.
    int from = low;
    int to = low;
    while (from > low - reach && at(from - 1) <= at(low) + 1.0) {
        --from;
    }
    while (to < low + reach && at(to + 1) <= at(low) + 1.0) {
        ++to;
    }
    low = (from + to) / 2;
    Section s;
    s.x = xs[static_cast<std::size_t>(low)];
    s.z = zs[static_cast<std::size_t>(low)];
    s.floor = at(low);
    // Its edges: walking out from its floor (over its flat part, which may fall a little along a
    // section across a bend), where its wall ends (the ground, once rising by 0.3 at least, rises
    // by less than 0.12), or where the ground falls 5 m under the floor without having risen as a
    // wall; 1,400 m out at most (the floor of a strait is wide).
    const auto edge = [&](int step) {
        bool steep = false;
        int i = low;
        const int most = static_cast<int>(1400.0 / kCrossStep);
        while (i + step >= 0 && i + step <= 2 * half && std::abs(i - low) < most) {
            const double rise = (at(i + step) - at(i)) / kCrossStep;
            if ((steep && rise < 0.12) || (!steep && at(i + step) < at(low) - 5.0)) {
                break;
            }
            steep = steep || rise >= 0.3;
            i += step;
        }
        return i;
    };
    const int a = edge(-1);
    const int b = edge(1);
    s.depth = std::min(at(a), at(b)) - s.floor;
    s.width = (b - a) * kCrossStep;
    s.slopeLeft = a < low ? (at(a) - s.floor) / ((low - a) * kCrossStep) : 0.0;
    s.slopeRight = b > low ? (at(b) - s.floor) / ((b - low) * kCrossStep) : 0.0;
    return s;
}

struct Gorge {
    std::vector<Section> path;
    double length = 0.0;
};

// The lip of a breach: within 20 degrees of its azimuth, the ray from the center of the caldera
// whose crest is lowest, at that crest; the direction of that ray. The crest of a ray: from 0.7
// radius out to 1.3, the first point higher than the next one, a quarter of the depth of the
// caldera over its start at least (the rim, before the ray leaves a gorge that bends); for a
// strait, the middle of its flat floor along the ray.
struct Lip {
    double x = 0.0;
    double z = 0.0;
    double crest = 1e9;
    double dx = 0.0;
    double dz = 0.0;
};

Lip lipOf(const terrain::Heightfield& field, const VolcanoSite& s,
          const terrain::volcano_relief::Shape& sh, bool strait, double cx, double cz) {
    const double ca = std::cos(static_cast<double>(s.axis));
    const double sa = std::sin(static_cast<double>(s.axis));
    const auto crater = static_cast<double>(s.crater);
    Lip lip;
    for (int k = -80; k <= 80; ++k) {
        const double a = sh.breachAngle + 20.0 * kPi / 180.0 * k / 80.0;
        const double u = std::cos(a) * static_cast<double>(s.elongation);
        const double v = std::sin(a);
        const double rx = u * ca - v * sa;
        const double rz = u * sa + v * ca;
        std::vector<double> xs;
        std::vector<double> zs;
        for (int i = 0; i <= 120; ++i) {
            const double r = crater * (0.7 + 0.6 * i / 120.0);
            xs.push_back(cx + r * rx);
            zs.push_back(cz + r * rz);
        }
        std::vector<float> h(xs.size());
        field.heightsAt(xs, zs, h);
        const float above = h[0] + 0.25f * s.craterDepth;
        const float highest = *std::max_element(h.begin(), h.end());
        std::size_t crest = 0;
        while (crest + 1 < h.size() &&
               !(strait ? h[crest] >= highest - 2.0f : h[crest] > above && h[crest + 1] < h[crest])) {
            ++crest;
        }
        if (strait) {
            std::size_t end = crest;
            while (end + 1 < h.size() && h[end + 1] >= highest - 2.0f) {
                ++end;
            }
            crest = (crest + end) / 2;
        }
        if (static_cast<double>(h[crest]) < lip.crest) {
            const double n = std::hypot(rx, rz);
            lip = {xs[crest], zs[crest], static_cast<double>(h[crest]), rx / n, rz / n};
        }
    }
    return lip;
}

// The thalweg from the lip down, while the gorge stays 10 m deep at least (a strait: out to the
// sea), over 1.2 times its length at most (GorgeShape::length; its floor may run a little
// farther, out to where the flank comes down to it) and down to the foot of the volcano.
Gorge traceGorge(const terrain::Heightfield& field, const VolcanoSite& s,
                 const terrain::volcano_relief::Shape& sh, bool strait) {
    const terrain::VolcanoOffset top = terrain::volcanoSummit(s);
    const double cx = s.x + top.dx;
    const double cz = s.z + top.dz;
    const Lip lip = lipOf(field, s, sh, strait, cx, cz);
    double dx = lip.dx;
    double dz = lip.dz;
    Gorge g;
    g.path.push_back(sectionAt(field, lip.x, lip.z, dx, dz, 60.0));
    // Down to the foot of the volcano at most.
    const double foot = 0.95 * static_cast<double>(s.radius) *
                        std::max(1.0, static_cast<double>(s.elongation));
    double travelled = 0.0;
    const double length = 1.2 * sh.gorge.length;
    while (travelled < length &&
           std::hypot(g.path.back().x - cx, g.path.back().z - cz) < foot) {
        const Section& last = g.path.back();
        const double search = std::clamp(0.4 * last.width, 30.0, 200.0);
        const Section next = sectionAt(field, last.x + kStep * dx, last.z + kStep * dz, dx, dz,
                                       search);
        if (strait ? (next.floor > 0.0 || std::max(next.floor + next.depth, 0.0) <= 0.0)
                   : next.depth < 10.0) {
            break;
        }
        const double step = std::hypot(next.x - last.x, next.z - last.z);
        travelled += step;
        g.length += step;
        g.path.push_back(next);
        // The direction: along the last three steps (one step zigzags across a bend).
        const Section& back = g.path[g.path.size() >= 4 ? g.path.size() - 4 : 0];
        const double bx = next.x - back.x;
        const double bz = next.z - back.z;
        const double n = std::hypot(bx, bz);
        if (n > 1.0) {
            dx = bx / n;
            dz = bz / n;
        }
    }
    return g;
}

const Section& sectionAlong(const Gorge& g, double share) {
    double run = 0.0;
    for (std::size_t i = 1; i < g.path.size(); ++i) {
        run += std::hypot(g.path[i].x - g.path[i - 1].x, g.path[i].z - g.path[i - 1].z);
        if (run >= share * g.length) {
            return g.path[i];
        }
    }
    return g.path.back();
}

// The mean over the sections from 1 to 4 steps past the lip (on the lip itself, a section along
// the rim dips into the caldera).
Section lipOf(const Gorge& g) {
    Section m;
    const std::size_t last = std::min<std::size_t>(g.path.size() - 1, 4);
    for (std::size_t i = 1; i <= last; ++i) {
        m.width += g.path[i].width / static_cast<double>(last);
        m.depth += g.path[i].depth / static_cast<double>(last);
    }
    return m;
}

double spillOf(const terrain::Heightfield& field, const VolcanoSite& s, std::size_t index) {
    const double r = 1.5 * static_cast<double>(s.crater) * static_cast<double>(s.elongation);
    for (const terrain::VolcanoBasin& b :
         field.volcanoBasinsIn(s.x - r, s.z - r, s.x + r, s.z + r)) {
        if (!b.maar && b.volcano == index) {
            return static_cast<double>(b.spill);
        }
    }
    return -1e9;
}

// Measures one breach; false where it fails.
bool testBreach(const terrain::Heightfield& field, const VolcanoSite& s, std::size_t index,
                const terrain::volcano_relief::Shape& sh) {
    // Every breach of an island caldera is a strait under the sea (20 to 80 m deep).
    const bool strait = s.form == VolcanoForm::IslandCaldera;
    const Gorge g = traceGorge(field, s, sh, strait);
    const Section& first = g.path.front();
    if (strait && g.path.size() < 3 && first.floor + first.depth <= 0.0) {
        // Its breach lies in a gap of its ring already under the sea (a ring of islands):
        // nothing to follow out to the sea.
        const bool ok = first.floor < -10.0;
        std::printf("%-15s %.0f %.0f: a strait in a gap of its ring under the sea, its floor at "
                    "%.0f m%s\n",
                    terrain::volcanoFormName(s.form), s.x, s.z, first.floor, ok ? "" : "  WRONG");
        return ok;
    }
    if (g.path.size() < 3) {
        std::printf("%-15s %.0f %.0f: no gorge traced  WRONG\n", terrain::volcanoFormName(s.form),
                    s.x, s.z);
        return false;
    }
    const Section& a = g.path.front();
    const Section& b = g.path.back();
    const double chord = std::hypot(b.x - a.x, b.z - a.z);
    const double sinuosity = g.length / std::max(chord, 1.0);
    const Section lip = lipOf(g);
    const Section& wide = sectionAlong(g, 0.7);
    const Section& deep = sectionAlong(g, 0.4);
    double rise = 0.0;
    double lowest = g.path.front().floor;
    double steeper = 0.0;
    double gentler = 1e9;
    for (const Section& c : g.path) {
        rise = std::max(rise, c.floor - lowest);
        lowest = std::min(lowest, c.floor);
        if (c.slopeLeft > 0.0 && c.slopeRight > 0.0) {
            const double ratio = std::log(c.slopeLeft / c.slopeRight);
            steeper = std::max(steeper, ratio);
            gentler = std::min(gentler, ratio);
        }
    }
    const double spill = spillOf(field, s, index);
    const double unevenWalls = std::exp(std::max(steeper, -gentler));
    bool ok = rise <= 1.0 && spill <= a.floor + 1.0 && unevenWalls >= 1.3;
    if (strait) {
        ok = ok && a.floor < -10.0 && lowest < -10.0;
    } else {
        ok = ok && sinuosity >= 1.1 && wide.width >= 1.8 * lip.width &&
             deep.depth >= 1.2 * lip.depth;
    }
    std::printf("%-15s %.0f %.0f: %4.0f m long, sinuosity %.2f; width %3.0f m at the lip, %3.0f "
                "0.7 of the way (x%.1f); depth %3.0f m at the lip, %3.0f 0.4 of the way (x%.1f); "
                "walls up to x%.1f steeper on one side; floor rises %.1f m; lip %.0f m, spill "
                "%.0f m%s\n",
                terrain::volcanoFormName(s.form), s.x, s.z, g.length, sinuosity, lip.width,
                wide.width, wide.width / std::max(lip.width, 1.0), lip.depth, deep.depth,
                deep.depth / std::max(lip.depth, 1.0), unevenWalls, rise, a.floor, spill,
                ok ? "" : "  WRONG");
    return ok;
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, volcano_test::on());
    const auto& sites = field.volcanoes()->sites();
    int land = 0;
    int straits = 0;
    int wrong = 0;
    int caldera = 0;
    int plateau = 0;
    for (std::size_t index = 0; index < sites.size(); ++index) {
        const VolcanoSite& s = sites[index];
        const terrain::volcano_relief::Shape sh = terrain::volcano_relief::shapeOf(s);
        int* count = s.form == VolcanoForm::Caldera          ? &caldera
                     : s.form == VolcanoForm::PlateauCaldera ? &plateau
                     : s.form == VolcanoForm::IslandCaldera  ? &straits
                                                             : nullptr;
        if (count == nullptr || !sh.breached || *count >= 3 || s.atSea != (count == &straits)) {
            continue;
        }
        ++*count;
        land += count == &straits ? 0 : 1;
        wrong += testBreach(field, s, index, sh) ? 0 : 1;
    }
    std::printf("%d gorges on land, %d straits, %d wrong\n", land, straits, wrong);
    TEST_CHECK(land >= 4 && straits >= 2 && wrong == 0);
    return test::result();
}
