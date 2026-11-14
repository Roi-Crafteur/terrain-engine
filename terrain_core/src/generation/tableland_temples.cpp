#include "tableland_temples.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::tableland_relief {

namespace {

// The sites: one to a cell of a lattice of kSiteCell meters, in kSiteOpen of the cells, jittered
// over kSiteJitter of a cell; their radius kRadiusLow to kRadiusLow + kRadiusSpread meters, stretched
// along a heading by up to kStretchMost (an ellipse of that radius over its half-axes, their ratio
// up to its square: never a flat side, unlike strong lobes, whose hollows flatten), bent by lobes
// kLobes times to the turn, each up to kLobeMost of it (too weak to flatten a hollow).
constexpr double kSiteCell = 1'550.0;
constexpr double kSiteOpen = 0.95;
constexpr double kSiteJitter = 0.8;
constexpr double kRadiusLow = 120.0;
constexpr double kRadiusSpread = 180.0;
constexpr double kStretchMost = 1.3;
constexpr std::array<double, 2> kLobes{3.0, 5.0};
constexpr std::array<double, 2> kLobeMost{0.03, 0.015};
// The temple needs its talus and kGap meters of the platform all around (kJagReach more for its
// jags): its center is moved across the canyon as little as that takes, kPull meters at most (to
// the middle of the platform where it is too narrow), and the temple is no wider than the platform
// holds it there, down to kRadiusLeast, fading out from there to kRadiusGone.
constexpr double kPull = 450.0;
constexpr double kGap = 50.0;
constexpr double kRadiusLeast = 100.0;
constexpr double kRadiusGone = 60.0;
// The outline moved by kJag meters of a noise of kJagWavelength and kJagFine of one of
// kJagFineWavelength, plus the rough outline of the cliffs: kJagReach kept off the platform's edges
// (nearly always enough), kJagMost at most (the reach of the sites).
constexpr double kJag = 120.0;
constexpr double kJagWavelength = 300.0;
constexpr double kJagFine = 120.0;
constexpr double kJagFineWavelength = 150.0;
constexpr double kJagReach = 120.0;
constexpr double kJagMost = 250.0;
// The jags of a temple are those of its outline as wide as kJagRadius meters at least, scaled with
// its radius below (never longer than a small temple: no blade along a ridge of their noise).
constexpr double kJagRadius = 250.0;
// Their tops kDropLow to kDropHigh of the depth of the canyon under its rim; their lower storey
// kStoreyLow to kStoreyHigh of their height; the upper one the same plan, its radius kSetbackLow
// to kSetbackHigh of it shorter, its rim at least its apron and a ledge kLedgeLow to kLedgeHigh
// meters wide inside that of the lower; their taluses of mean slope kTalus (the lower: steeper
// where its apron would pass kApronMost, the reach of the sites), kUpperTalus (the upper).
constexpr double kDropLow = 0.1;
constexpr double kDropHigh = 0.3;
constexpr double kStoreyLow = 0.4;
constexpr double kStoreyHigh = 0.65;
constexpr double kSetbackLow = 0.3;
constexpr double kSetbackHigh = 0.5;
constexpr double kSetbackMost = 0.65;
constexpr double kLedgeLow = 10.0;
constexpr double kLedgeHigh = 40.0;
constexpr double kTalus = 1.0;
constexpr double kUpperTalus = 1.5;
constexpr double kApronMost = 400.0;
// Beyond the foot of its talus, its skirt goes down at kSkirt for kSkirtReach meters at most, to the
// ground of the canyon wherever it meets it (its platform deeper there than at its center).
constexpr double kSkirt = 1.2;
constexpr double kSkirtReach = 180.0;
// The offset of the outline is measured by differences over kOffsetStep meters; the distances read
// over their gradient, kLeastStretch at least, the bend of the lobes no nearer the center than
// kCore of the radius (inside, only the sign counts).
constexpr double kOffsetStep = 4.0;
constexpr double kLeastStretch = 0.3;
constexpr double kCore = 0.5;
// The reach of a site: its widest radius, its jags, its apron and its pull, no farther than its
// cell (the 3 x 3 cells around a column hold every site that reaches it).
static_assert((kRadiusLow + kRadiusSpread) * kStretchMost * (1.0 + 0.03 + 0.015) + kJagMost +
                      kApronMost + kSkirtReach + kPull <
                  kSiteCell * (1.0 + 0.5 * (1.0 - kSiteJitter)),
              "a site reaches beyond the cells around a column");

noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, double wavelength, int count,
                          const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = count,
                              .frequency = 1.0 / wavelength,
                              .lattices = noise::OctaveLattices::RotatedFast},
                             world);
}

// A share in [0, 1) of the hash h, from its bits from shift up.
double draw(std::uint64_t h, int shift) {
    return static_cast<double>((h >> shift) & 0xFFFFU) / 65536.0;
}

// The offset of the outline of the temples at (x, z), meters.
double offsetAt(const TempleNoises& n, const CliffNoises& walls, double x, double z) {
    const double u = noise::wrapCanonical(x, n.world.x);
    const double v = noise::wrapCanonical(z, n.world.z);
    return kJag * static_cast<double>(n.jag.fbm2D(u, v)) +
           kJagFine * static_cast<double>(n.jagFine.fbm2D(u, v)) + roughAt(walls, u, v);
}

// The site of the cell (i, j) of the lattice (counted from the corner of the world), its cells
// cellX by cellZ meters; false where the cell holds none.
bool siteOf(const TempleNoises& n, std::int64_t i, std::int64_t j, double cellX, double cellZ,
            TempleSite& site) {
    const std::int64_t wi = ((i % n.cellsX) + n.cellsX) % n.cellsX;
    const std::int64_t wj = ((j % n.cellsZ) + n.cellsZ) % n.cellsZ;
    const std::uint64_t h =
        noise::deriveSeed(noise::deriveSeed(n.siteSeed, static_cast<std::uint64_t>(wi)),
                          static_cast<std::uint64_t>(wj));
    if (!(draw(h, 0) < kSiteOpen)) {
        return false;
    }
    const std::uint64_t lobes = noise::deriveSeed(h, 1);
    const std::uint64_t phases = noise::deriveSeed(h, 2);
    const std::uint64_t more = noise::deriveSeed(h, 3);
    const double margin = 0.5 * (1.0 - kSiteJitter);
    site.x = (static_cast<double>(i) + margin + kSiteJitter * draw(h, 16)) * cellX - 0.5 * n.world.x;
    site.z = (static_cast<double>(j) + margin + kSiteJitter * draw(h, 32)) * cellZ - 0.5 * n.world.z;
    site.radius = kRadiusLow + kRadiusSpread * draw(h, 48);
    for (std::size_t k = 0; k < kLobes.size(); ++k) {
        const int shift = 16 * static_cast<int>(k);
        site.lobe[k] = kLobeMost[k] * draw(lobes, shift);
        site.phase[k] = draw(phases, shift);
    }
    site.stretch = 1.0 + (kStretchMost - 1.0) * draw(lobes, 32);
    site.heading = draw(phases, 32);
    site.drop = draw(lobes, 48);
    site.storey = draw(phases, 48);
    site.ledge = draw(more, 0);
    site.setback = draw(more, 16);
    return true;
}

// The widest and the narrowest of the plan of s, over its radius.
double bendMost(const TempleSite& s) {
    return s.stretch * (1.0 + s.lobe[0] + s.lobe[1]);
}
double bendLeast(const TempleSite& s) {
    return (1.0 - s.lobe[0] - s.lobe[1]) / s.stretch;
}

// The plan of s toward turns (a fraction of a turn from +x): its radius over that of s, and how
// fast it changes per radian.
struct Bend {
    double value = 1.0;
    double slope = 0.0;
};
Bend bendAt(const TempleSite& s, double turns) {
    // The ellipse of half-axes stretch and 1 / stretch, its long one along the heading.
    const dmath::SineCosine h = dmath::sinCosTurns(turns - s.heading);
    const double longer = s.stretch * s.stretch;
    const double shorter = 1.0 / longer;
    const double q = shorter * h.cosine * h.cosine + longer * h.sine * h.sine;
    const double ellipse = 1.0 / std::sqrt(q);
    const double ellipseSlope = -(longer - shorter) * h.sine * h.cosine * ellipse / q;
    double lobes = 1.0;
    double lobesSlope = 0.0;
    for (std::size_t k = 0; k < kLobes.size(); ++k) {
        const dmath::SineCosine sc = dmath::sinCosTurns(kLobes[k] * turns + s.phase[k]);
        lobes += s.lobe[k] * sc.sine;
        lobesSlope += s.lobe[k] * kLobes[k] * sc.cosine;
    }
    return {ellipse * lobes, ellipseSlope * lobes + ellipse * lobesSlope};
}

// The heights of a temple: its top drop meters under the rim, its storeys lower and upper meters
// high, their caprocks, their aprons, its lower talus; none where its height vanishes.
struct Heights {
    double drop = 0.0;
    double lower = 0.0;
    double upper = 0.0;
    double capLower = 0.0;
    double capUpper = 0.0;
    double apron = 0.0;
    double upperApron = 0.0;
    double talus = kTalus;
};

Heights heightsOf(const TempleSite& s, double deep, double platform, double cliff, double steep,
                  double scale) {
    Heights h;
    // Faded, it sinks into its platform whole.
    const double height = scale * (platform - deep * (kDropLow + (kDropHigh - kDropLow) * s.drop));
    if (!(height > 0.0)) {
        return h;
    }
    h.drop = platform - height;
    h.lower = height * (kStoreyLow + (kStoreyHigh - kStoreyLow) * s.storey);
    h.upper = height - h.lower;
    h.capLower = cliff * h.lower;
    h.capUpper = cliff * h.upper;
    h.talus = std::max(kTalus, (h.lower - h.capLower) / std::max(1.0, kApronMost - h.capLower / steep));
    h.apron = h.capLower / steep + (h.lower - h.capLower) / h.talus;
    h.upperApron = h.capUpper / steep + (h.upper - h.capUpper) / kUpperTalus;
    return h;
}

// A storey of a temple at a column: its outline (meters, positive within), the unit gradient of it.
struct Plan {
    double outline = 0.0;
    double dirX = 0.0;
    double dirZ = 0.0;
};

// The plan of a storey of radius radius of s at (dx, dz) from its center, its outline moved by
// jag (gradient (jagX, jagZ)).
Plan planOf(const TempleSite& s, double radius, double dx, double dz, double jag, double jagX,
            double jagZ) {
    const double d = dmath::hypot(dx, dz);
    const double turns = d > 0.0 ? dmath::atan2(dz, dx) / (2.0 * 3.141592653589793) : 0.0;
    const Bend bend = bendAt(s, turns);
    const double rx = d > 0.0 ? dx / d : 0.0;
    const double rz = d > 0.0 ? dz / d : 0.0;
    const double tangent = radius * bend.slope / std::max(d, kCore * radius);
    const double gx = -rx - tangent * rz + jagX;
    const double gz = -rz + tangent * rx + jagZ;
    const double g = dmath::hypot(gx, gz);
    const double unit = std::max(g, kLeastGradient);
    return {(radius * bend.value - d + jag) / std::max(g, kLeastStretch), gx / unit, gz / unit};
}

// Where a column stands from the center of a temple, and the offset of its outline there.
struct Offset {
    double dx = 0.0;
    double dz = 0.0;
    double jag = 0.0;
    double jagX = 0.0;
    double jagZ = 0.0;
};

// The temple of s of heights h and radius radius, its cliffs of slope steep, at o.
Temple storeysOf(const TempleSite& s, const Heights& h, double radius, double steep,
                 const Offset& o) {
    Temple out;
    const Plan low = planOf(s, radius, o.dx, o.dz, o.jag, o.jagX, o.jagZ);
    out.outline = low.outline;
    const double base = h.drop + h.lower + h.upper;
    const Storeys first = storeysAt(low.outline, h.lower, h.capLower, h.talus, steep);
    if (!(first.share > 0.0)) {
        const double beyond = -low.outline - h.apron; // into its skirt
        if (beyond < kSkirtReach) {
            out.stands = true;
            out.skirt = true;
            out.depth = base + kSkirt * std::max(0.0, beyond);
            out.gradX = -kSkirt * low.dirX;
            out.gradZ = -kSkirt * low.dirZ; // no hard layer of its own: that of the canyon
        }
        return out;
    }
    // The upper storey: its rim at least its apron and a ledge inside that of the lower, where its
    // plan is narrowest, set back by kSetbackMost of the radius at most (its talus steeper where that
    // leaves too little room: its top never a point).
    const double ledge = kLedgeLow + (kLedgeHigh - kLedgeLow) * s.ledge;
    const double least = bendLeast(s);
    const double setback =
        std::min(kSetbackMost * radius,
                 std::max(radius * (kSetbackLow + (kSetbackHigh - kSetbackLow) * s.setback),
                          (h.upperApron + ledge) / least));
    const double run = setback * least - ledge - h.capUpper / steep; // of its talus
    const double talus = std::max(kUpperTalus, (h.upper - h.capUpper) / std::max(1.0, run));
    const Plan up = planOf(s, radius - setback, o.dx, o.dz, o.jag, o.jagX, o.jagZ);
    const Storeys second = storeysAt(up.outline, h.upper, h.capUpper, talus, steep);
    out.stands = true;
    out.depth = base - (h.lower * first.share + h.upper * second.share);
    out.gradX = -(h.lower * first.rate * low.dirX + h.upper * second.rate * up.dirX);
    out.gradZ = -(h.lower * first.rate * low.dirZ + h.upper * second.rate * up.dirZ);
    if (second.share > 0.0) {
        out.unit = {h.drop + kUnderTop, h.capUpper, true};
        out.cliff = second.cliff;
    } else {
        out.unit = {h.drop + h.upper + kUnderTop, h.capLower, true};
        out.cliff = first.cliff;
    }
    return out;
}

// The temple of s (span: the canyon at its center) at (x, z).
Temple templeOf(const TempleSite& s, const TempleSpan& span, double x, double z, double jag,
                double jagX, double jagZ, double cliff, double steep) {
    Temple out;
    if (!(span.to > span.from) || !(span.deep > 0.0)) {
        return out;
    }
    const Heights whole = heightsOf(s, span.deep, span.platform, cliff, steep, 1.0);
    const double margin = whole.apron + kGap + kJagReach;
    // Its center moved as little as it takes to hold it whole (to the middle where nothing does).
    const double nearest = span.from + margin + s.radius * bendMost(s);
    const double farthest = span.to - margin - s.radius * bendMost(s);
    const double target = nearest < farthest ? std::clamp(span.distance, nearest, farthest)
                                             : 0.5 * (nearest + farthest);
    const double pull = std::clamp(target - span.distance, -kPull, kPull);
    const double distance = span.distance + pull;
    const double dx = x - (s.x + pull * span.dirX);
    const double dz = z - (s.z + pull * span.dirZ);
    const double room = std::min(distance - span.from, span.to - distance) - margin;
    const double radius = std::min(s.radius, room / bendMost(s));
    const double fade = smooth(kRadiusGone, kRadiusLeast, radius);
    if (!(fade > 0.0)) {
        return out;
    }
    const Heights h = heightsOf(s, span.deep, span.platform, cliff, steep, fade);
    if (!(h.lower > 0.0)) {
        return out;
    }
    const double jagged = std::min(1.0, radius / kJagRadius);
    return storeysOf(s, h, radius, steep,
                     Offset{dx, dz, jagged * jag, jagged * jagX, jagged * jagZ});
}

} // namespace

TempleNoises makeTempleNoises(std::uint64_t seed, const WorldSize& world) {
    return {octaves(seed, 42, kJagWavelength, 2, world),
            octaves(seed, 43, kJagFineWavelength, 2, world),
            noise::deriveSeed(seed, 22),
            std::max<std::int64_t>(1, std::llround(world.x / kSiteCell)),
            std::max<std::int64_t>(1, std::llround(world.z / kSiteCell)),
            world};
}

TempleSites templeSitesNear(const TempleNoises& n, double x, double z, double deep, double cliff,
                            double steep) {
    TempleSites out;
    const double lower = (1.0 - kDropLow) * deep * kStoreyHigh;
    const double apron = std::min(kApronMost, lower * (cliff / steep + (1.0 - cliff) / kTalus));
    const double cellX = n.world.x / static_cast<double>(n.cellsX);
    const double cellZ = n.world.z / static_cast<double>(n.cellsZ);
    const auto i = static_cast<std::int64_t>(std::floor((x + 0.5 * n.world.x) / cellX));
    const auto j = static_cast<std::int64_t>(std::floor((z + 0.5 * n.world.z) / cellZ));
    for (std::int64_t dj = -1; dj <= 1; ++dj) {
        for (std::int64_t di = -1; di <= 1; ++di) {
            TempleSite s;
            if (!siteOf(n, i + di, j + dj, cellX, cellZ, s)) {
                continue;
            }
            const double reach = s.radius * bendMost(s) + kJagMost + apron + kSkirtReach + kPull;
            if (dmath::hypot(x - s.x, z - s.z) < reach) {
                out.site[out.count++] = s;
            }
        }
    }
    return out;
}

Temple templeAt(const TempleNoises& n, double x, double z, const TempleSites& sites,
                const std::array<TempleSpan, kMostTempleSites>& spans, double cliff, double steep,
                const CliffNoises& walls) {
    Temple out;
    if (sites.count == 0) {
        return out;
    }
    const double jag = offsetAt(n, walls, x, z);
    const double jagX = (offsetAt(n, walls, x + kOffsetStep, z) - jag) / kOffsetStep;
    const double jagZ = (offsetAt(n, walls, x, z + kOffsetStep) - jag) / kOffsetStep;
    bool any = false;
    for (std::size_t k = 0; k < sites.count; ++k) {
        const TempleSite& s = sites.site[k];
        const Temple t = templeOf(s, spans[k], x, z, jag, jagX, jagZ, cliff, steep);
        if (!t.stands) {
            continue;
        }
        // Where two temples meet, the higher ground.
        if (!any || t.depth < out.depth) {
            const double outline = any ? std::max(out.outline, t.outline) : t.outline;
            out = t;
            out.outline = outline;
        } else {
            out.outline = std::max(out.outline, t.outline);
        }
        any = true;
    }
    return out;
}

} // namespace terrain::tableland_relief
