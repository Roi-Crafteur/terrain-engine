#include "tableland_cliffs.hpp"

#include "mountain_walls.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::tableland_relief {

namespace {

// The outline: kRough meters of a noise of kRoughWavelength (2 octaves: 160, 80 m: alcoves and
// buttresses, the whole height of a cliff) and kGrit of one of kGritWavelength (40, 20 m: its flutes,
// each layer its own): their slopes under 1 (an outline that never folds into slots and needles).
constexpr double kRough = 18.0;
constexpr double kRoughWavelength = 160.0;
constexpr double kGrit = 3.0;
constexpr double kGritWavelength = 40.0;
// The batter: the slope of the cliffs from kSteepLow (78 degrees) to kSteepHigh (85 degrees) by a
// noise of kSteepWavelength over its middle (kSteepSpread each side of 0); with the vertical faces
// of their hard layers and their ledges, 80 to 88 degrees.
constexpr double kSteepLow = 4.705;
constexpr double kSteepHigh = 11.430;
constexpr double kSteepSpread = 0.3;
constexpr double kSteepWavelength = 2'000.0;
// The ledges of a caprock: at the bounds of its layers over its foot (kLedgeFoot meters over it at
// least), they take the run its batter leaves to its walls, these as steep as kWallSlope (89
// degrees: the rock lays no tread of its own on them). Each bound draws a noise of kLedgeWavelength
// (read kLedgeApart meters away for each bound: their own); the bounds within kLedgeMargin of the
// greatest draw share the run (by the square of their lead over it less the margin: mostly one
// ledge, two where their draws meet, never a ledge popping up), each kLedgeHigh meters wide at
// most; the leading one kLedgeLow wide at least (taken from the batter where its run is short),
// the others as their lead goes, fading out under half of that (never a band of narrow ones). Each
// bound besides on a bench kBench meters wide (as the run allows: the walls of two layers never
// cross). Each layer's flutes are those of the layer over it
// turned inside out (its flutes where the ribs of the next stand), shifted along the outline by up
// to kShiftSpread of a flute (kFluteSpacing meters apart, drawn per layer), leaning by up to
// kLeanMost (tan 10 degrees, a noise of kLeanWavelength).
constexpr double kLedgeWavelength = 2'500.0;
constexpr double kLedgeApart = 50'000.0;
constexpr double kLedgeMargin = 0.05;
constexpr double kLedgeLow = 10.0;
constexpr double kLedgeHigh = 24.0;
constexpr double kBench = 1.5;
constexpr double kLedgeFoot = 10.0;
constexpr double kWallSlope = 57.3;
constexpr double kFluteSpacing = 25.0;
constexpr double kShiftSpread = 0.1;
constexpr double kLeanMost = 0.176;
constexpr double kLeanWavelength = 3'000.0;
constexpr std::uint64_t kShiftSalt = 0x7368696674ULL;
// The hard layers of a caprock: kLayersLow to kLayersHigh of them (a noise of kLayersWavelength:
// the buttes a few kilometers apart differ).
constexpr double kLayersLow = 2.5;
constexpr double kLayersHigh = 4.0;
constexpr double kLayersWavelength = 2'500.0;
// The blocks of scree: at most one per cell of about kBlockCell meters, its center anywhere in the
// cell (kBlockJitter of it), kBlockRadiusLow to kBlockRadiusLow + kBlockRadiusSpread meters wide,
// kBlockTallLow to kBlockTallLow + kBlockTallSpread of that high; laid in kBlockOpenMost of the cells
// at most where a noise of kBlockWavelength opens them (kBlockOpenLow to kBlockOpenHigh), thinning
// out over kBlockReach meters of the talus from the foot of the cliff: no row of them.
constexpr double kBlockCell = 30.0;
constexpr double kBlockJitter = 1.0;
constexpr double kBlockOpenMost = 0.5;
constexpr double kBlockRadiusLow = 4.0;
constexpr double kBlockRadiusSpread = 8.0;
constexpr double kBlockTallLow = 0.35;
constexpr double kBlockTallSpread = 0.45;
constexpr double kBlockWavelength = 400.0;
constexpr double kBlockOpenLow = -0.35;
constexpr double kBlockOpenHigh = 0.45;
constexpr double kBlockReach = 80.0;
constexpr double kBlockIn = 5.0;
// The cones of scree under the re-entrants of the outline (it recedes kConeFrom to kConeFull of
// kRough), under the cliffs kConeLow to kConeHigh meters high and more.
constexpr double kConeFrom = 0.1;
constexpr double kConeFull = 0.6;
constexpr double kConeLow = 20.0;
constexpr double kConeHigh = 60.0;
// The joints of the faces: open where a noise of kJointsWavelength passes kJointsLow to
// kJointsHigh (in clusters: most of the walls without), from kJointsDeep to kJointsDeep +
// kJointsSpread of their depth as it goes on rising.
constexpr double kJointsWavelength = 300.0;
constexpr double kJointsLow = 0.0;
constexpr double kJointsHigh = 0.35;
constexpr double kJointsDeep = 0.4;
constexpr double kJointsSpread = 1.6;

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

double noiseAt(const noise::FbmOctaves& f, double x, double z) {
    return static_cast<double>(f.fbm2D(x, z));
}

// The height of the block of the cell (i, j) of the lattice at (x, z) (0 off it): cell is its size
// along x and z, open how many of the cells hold one.
double blockOf(const CliffNoises& n, std::int64_t i, std::int64_t j, double cellX, double cellZ,
               double x, double z, double open) {
    const std::int64_t wi = ((i % n.cellsX) + n.cellsX) % n.cellsX;
    const std::int64_t wj = ((j % n.cellsZ) + n.cellsZ) % n.cellsZ;
    const std::uint64_t h =
        noise::deriveSeed(noise::deriveSeed(n.blockSeed, static_cast<std::uint64_t>(wi)),
                          static_cast<std::uint64_t>(wj));
    if (!(draw(h, 0) < open)) {
        return 0.0;
    }
    const double margin = 0.5 * (1.0 - kBlockJitter);
    const double cx = (static_cast<double>(i) + margin + kBlockJitter * draw(h, 16)) * cellX;
    const double cz = (static_cast<double>(j) + margin + kBlockJitter * draw(h, 32)) * cellZ;
    const double radius = kBlockRadiusLow + kBlockRadiusSpread * draw(h, 48);
    const double dx = x - cx;
    const double dz = z - cz;
    const double t = 1.0 - (dx * dx + dz * dz) / (radius * radius);
    if (!(t > 0.0)) {
        return 0.0;
    }
    const double tall = radius * (kBlockTallLow + kBlockTallSpread * draw(h ^ (h >> 29), 8));
    return tall * t * t;
}

} // namespace

CliffNoises makeCliffNoises(std::uint64_t seed, const WorldSize& world) {
    return {octaves(seed, 34, kRoughWavelength, 2, world),
            octaves(seed, 35, kGritWavelength, 2, world),
            octaves(seed, 36, kSteepWavelength, 2, world),
            octaves(seed, 37, kLayersWavelength, 2, world),
            octaves(seed, 39, kBlockWavelength, 2, world),
            octaves(seed, 40, kJointsWavelength, 2, world),
            octaves(seed, 38, kLeanWavelength, 2, world),
            octaves(seed, 33, kLedgeWavelength, 2, world),
            noise::deriveSeed(seed, 41),
            std::max<std::int64_t>(1, std::llround(world.x / kBlockCell)),
            std::max<std::int64_t>(1, std::llround(world.z / kBlockCell)),
            world};
}

double roughAt(const CliffNoises& n, double x, double z) {
    return kRough * noiseAt(n.rough, x, z) + kGrit * noiseAt(n.grit, x, z);
}

double steepAt(const CliffNoises& n, double x, double z) {
    return kSteepLow +
           (kSteepHigh - kSteepLow) * smooth(-kSteepSpread, kSteepSpread, noiseAt(n.steep, x, z));
}

double layersAt(const CliffNoises& n, double x, double z) {
    return kLayersLow + (kLayersHigh - kLayersLow) * smooth(-0.5, 0.5, noiseAt(n.layers, x, z));
}

Scree screeAt(const CliffNoises& n, double x, double z, double foot, double cliff, double rough) {
    Scree out;
    if (!(cliff > 0.0)) {
        return out;
    }
    out.cone = smooth(kConeFrom * kRough, kConeFull * kRough, -rough) *
               smooth(kConeLow, kConeHigh, cliff);
    if (!(foot > 0.0) || !(foot < kBlockReach)) {
        return out;
    }
    // The blocks: the nearest cells of the lattice (canonical (x, z), its cells counted from the
    // corner of the world).
    const double open = kBlockOpenMost * smooth(kBlockOpenLow, kBlockOpenHigh, noiseAt(n.blocks, x, z)) *
                        smooth(0.0, kBlockIn, foot) * (1.0 - smooth(0.0, kBlockReach, foot));
    if (!(open > 0.0)) {
        return out;
    }
    const double cellX = n.world.x / static_cast<double>(n.cellsX);
    const double cellZ = n.world.z / static_cast<double>(n.cellsZ);
    const double u = x + 0.5 * n.world.x;
    const double v = z + 0.5 * n.world.z;
    const auto i = static_cast<std::int64_t>(std::floor(u / cellX));
    const auto j = static_cast<std::int64_t>(std::floor(v / cellZ));
    for (std::int64_t dj = -1; dj <= 1; ++dj) {
        for (std::int64_t di = -1; di <= 1; ++di) {
            out.blocks = std::max(out.blocks, blockOf(n, i + di, j + dj, cellX, cellZ, u, v, open));
        }
    }
    return out;
}

Ledges ledgesAt(const CliffNoises& n, std::uint64_t units, double x, double z, double cliff,
                double steep) {
    if (!(cliff > 0.0)) {
        Ledges out;
        out.wall = steep;
        return out;
    }
    const double band = cliff / layersAt(n, x, z);
    const double top = mountain_walls::layerBound(units, kTopLayerOfUnit);
    // The bounds over the foot, from the top down, and their draws.
    Ledges bounds;
    std::array<double, kLedgesMost> draws{};
    double best = -1.0;
    for (std::int64_t k = kTopLayerOfUnit - 1; bounds.count < kLedgesMost; --k) {
        const double drop = kUnderTop + band * (top - mountain_walls::layerBound(units, k));
        if (!(drop < cliff - kLedgeFoot)) {
            break;
        }
        const double apart = kLedgeApart * static_cast<double>(k);
        draws[bounds.count] =
            noiseAt(n.ledges, noise::wrapCanonical(x + apart, n.world.x), z);
        best = std::max(best, draws[bounds.count]);
        bounds.held[bounds.count] = smooth(0.0, kLedgeFoot, cliff - kLedgeFoot - drop);
        bounds.drop[bounds.count] = drop;
        bounds.layer[bounds.count] = k - 1;
        ++bounds.count;
    }
    double sum = 0.0;
    std::array<double, kLedgesMost> leads{};
    for (std::size_t i = 0; i < bounds.count; ++i) {
        const double lead = std::max(0.0, draws[i] - best + kLedgeMargin);
        leads[i] = bounds.held[i] * lead * lead;
        sum += leads[i];
    }
    // Every bound a step of its own (its layer its own rough outline) on its bench, most of them
    // without ledge.
    const double room = cliff / steep - cliff / kWallSlope; // the run the ledges may take
    double held = 0.0;
    for (std::size_t i = 0; i < bounds.count; ++i) {
        held += bounds.held[i];
    }
    const double bench = held > 0.0 ? std::min(kBench, 0.5 * room / held) : 0.0;
    const double rest = room - bench * held;
    double taken = bench * held;
    double lead = 0.0;
    for (std::size_t i = 0; i < bounds.count; ++i) {
        lead = std::max(lead, leads[i]);
    }
    for (std::size_t i = 0; i < bounds.count; ++i) {
        // The ledge of the bound that leads kLedgeLow wide at least (as its lead goes, the others).
        const double share = sum > 0.0 ? rest * leads[i] / sum : 0.0;
        const double least = lead > 0.0 ? kLedgeLow * leads[i] / lead : 0.0;
        const double ledge = std::min(kLedgeHigh, std::max(share, least)) *
                             smooth(0.5 * kLedgeLow, kLedgeLow, least + share);
        bounds.width[i] = bench * bounds.held[i] + ledge;
        taken += ledge;
    }
    bounds.wall = cliff / std::max(cliff / steep - taken, cliff / kWallSlope);
    return bounds;
}

namespace {

// The rough outline of the wall of a cliff at (x, z) whose top layer is layer (of the stack), its
// outline along (alongX, alongZ), depth meters under the top of that wall: its alcoves those of the
// cliff, its flutes its own (shifted along the outline, leaning).
double layerRough(const CliffNoises& n, std::uint64_t units, double x, double z, double alongX,
                  double alongZ, std::int64_t layer, double depth) {
    const std::uint64_t h = noise::deriveSeed(units ^ kShiftSalt, static_cast<std::uint64_t>(layer));
    const double lean = kLeanMost * noiseAt(n.tilt, x, z);
    const double along = kFluteSpacing * kShiftSpread * (2.0 * draw(h, 0) - 1.0) + lean * depth;
    const double side = (layer & 1) != 0 ? -1.0 : 1.0;
    return kRough * noiseAt(n.rough, x, z) +
           side * kGrit *
               noiseAt(n.grit, noise::wrapCanonical(x + along * alongX, n.world.x),
                       noise::wrapCanonical(z + along * alongZ, n.world.z));
}

} // namespace

Storeys cliffStoreysAt(const CliffNoises& n, std::uint64_t units, double x, double z,
                       const Field& outline, double height, double cliff, double talus,
                       const Ledges& ledges) {
    if (!(height > 0.0)) {
        return {};
    }
    const double tall = std::clamp(cliff, 0.0, height);
    // Each wall a step of its own: from its nominal place outward (the walls and ledges over it) moved
    // by its own rough outline (its depth in it read at its nominal place), falling at the slope of
    // the walls; the cliff their sum (where two of them meet, no ledge: one wall).
    const double plain = -outline.value; // outward, the rough aside
    double drop = 0.0;
    double rate = 0.0;
    double place = 0.0; // nominal
    double foot = 0.0;  // where the walls end, moved by their rough (the farthest out)
    for (std::size_t k = 0; k <= ledges.count; ++k) {
        const double top = k == 0 ? 0.0 : std::min(ledges.drop[k - 1], tall);
        const double bottom = k < ledges.count ? std::min(ledges.drop[k], tall) : tall;
        const double run = (bottom - top) / ledges.wall;
        const double depth = std::clamp((plain - place) * ledges.wall, 0.0, bottom - top);
        const std::int64_t layer = k == 0 ? kTopLayerOfUnit - 1 : ledges.layer[k - 1];
        double rough = layerRough(n, units, x, z, -outline.dirZ, outline.dirX, layer, depth);
        if (k > 0 && ledges.held[k - 1] < 1.0) {
            // Near the foot, the rough of the layer over it, its depth going on.
            const std::int64_t over = k == 1 ? kTopLayerOfUnit - 1 : ledges.layer[k - 2];
            const double above = layerRough(n, units, x, z, -outline.dirZ, outline.dirX, over,
                                             depth + top - (k == 1 ? 0.0 : ledges.drop[k - 2]));
            rough = above + ledges.held[k - 1] * (rough - above);
        }
        const double at = place + rough;
        const double into = (plain - at) * ledges.wall;
        drop += std::clamp(into, 0.0, bottom - top);
        rate += into > 0.0 && into < bottom - top ? ledges.wall : 0.0;
        foot = k == 0 ? at + run : std::max(foot, at + run);
        place += run + (k < ledges.count ? ledges.width[k] : 0.0);
    }
    // Beyond the foot of every wall the cliff is whole (its sum of steps tall but for rounding).
    if (plain < foot) {
        return {1.0 - drop / height, rate / height, rate > 0.0};
    }
    // The talus from the foot of the last wall.
    const double slope = (height - tall) / talus;
    if (!(slope > 0.0) || !(plain < foot + slope)) {
        return {};
    }
    const double v = (foot + slope - plain) / slope;
    const double below = 1.0 - tall / height;
    const double root = std::sqrt(std::max(v, 0.0));
    return {below * v * root, below * 1.5 * root / slope, false};
}

double jointsAt(const CliffNoises& n, double x, double z) {
    const double v = noiseAt(n.joints, x, z);
    const double open = smooth(kJointsLow, kJointsHigh, v);
    return open * (kJointsDeep + kJointsSpread * smooth(kJointsHigh, 2.0 * kJointsHigh, v));
}

} // namespace terrain::tableland_relief
