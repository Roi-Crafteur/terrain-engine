#include "tableland_cliffs.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::tableland_relief {

namespace {

// The outline: kRough meters of a noise of kRoughWavelength (3 octaves: 160, 80, 40 m) and kGrit
// of one of kGritWavelength (40, 20 m): their slopes under 1 (an outline that never folds into
// slots and needles).
constexpr double kRough = 25.0;
constexpr double kRoughWavelength = 160.0;
constexpr double kGrit = 5.0;
constexpr double kGritWavelength = 40.0;
// The batter: the slope of the cliffs from kSteepLow (78 degrees) to kSteepHigh (85 degrees) by a
// noise of kSteepWavelength over its middle (kSteepSpread each side of 0); with the vertical faces
// of their hard layers and their ledges, 80 to 88 degrees.
constexpr double kSteepLow = 4.705;
constexpr double kSteepHigh = 11.430;
constexpr double kSteepSpread = 0.3;
constexpr double kSteepWavelength = 2'000.0;
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
    return {octaves(seed, 34, kRoughWavelength, 3, world),
            octaves(seed, 35, kGritWavelength, 2, world),
            octaves(seed, 36, kSteepWavelength, 2, world),
            octaves(seed, 37, kLayersWavelength, 2, world),
            octaves(seed, 39, kBlockWavelength, 2, world),
            octaves(seed, 40, kJointsWavelength, 2, world),
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
    if (!(foot >= 0.0) || !(cliff > 0.0)) {
        return out;
    }
    out.cone = smooth(kConeFrom * kRough, kConeFull * kRough, -rough) *
               smooth(kConeLow, kConeHigh, cliff);
    if (!(foot < kBlockReach)) {
        return out;
    }
    // The blocks: the nearest cells of the lattice (canonical (x, z), its cells counted from the
    // corner of the world).
    const double open = kBlockOpenMost * smooth(kBlockOpenLow, kBlockOpenHigh, noiseAt(n.blocks, x, z)) *
                        (1.0 - smooth(0.0, kBlockReach, foot));
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

double jointsAt(const CliffNoises& n, double x, double z) {
    const double v = noiseAt(n.joints, x, z);
    const double open = smooth(kJointsLow, kJointsHigh, v);
    return open * (kJointsDeep + kJointsSpread * smooth(kJointsHigh, 2.0 * kJointsHigh, v));
}

} // namespace terrain::tableland_relief
