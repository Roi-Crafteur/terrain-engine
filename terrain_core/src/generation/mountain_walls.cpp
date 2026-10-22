#include "mountain_walls.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::mountain_walls {

namespace {

// Where the walls stand: the cores of the chains (intensity kCoreLow to kCoreHigh) on faces
// steeper than kSteepLow to kSteepHigh; rare isolated regions (their noise of kIsolatedWavelength
// above kIsolatedLow to kIsolatedHigh) in the foothills and on the plateaus (intensity
// kFoothillsLow to kFoothillsHigh) on gentler faces (kGentleLow to kGentleHigh). On a face, the
// walls come in patches of about kPatchWavelength (their noise from kPatchLow to kPatchHigh): at
// least kPatchFloor of the mask everywhere.
constexpr float kCoreLow = 0.45f;
constexpr float kCoreHigh = 0.7f;
constexpr double kSteepLow = 0.2;
constexpr double kSteepHigh = 0.45;
constexpr double kIsolatedWavelength = 70'000.0;
constexpr float kIsolatedLow = 0.55f;
constexpr float kIsolatedHigh = 0.7f;
constexpr float kFoothillsLow = 0.12f;
constexpr float kFoothillsHigh = 0.3f;
constexpr double kGentleLow = 0.25;
constexpr double kGentleHigh = 0.5;
// Only on the great faces: the great forms kReliefLow to kReliefHigh meters above the base.
constexpr double kReliefLow = 100.0;
constexpr double kReliefHigh = 400.0;
constexpr double kPatchWavelength = 1'500.0;
constexpr double kPatchLow = -0.9;
constexpr double kPatchHigh = -0.5;
constexpr double kPatchFloor = 0.0;
// Strata or crystalline: a noise of kCharacterWavelength, plus kWornStrata per unit of character
// below kSharp (the worn massifs and the plateaus are layered), plus kIsolatedStrata in the
// isolated regions, minus kCrystalCores in the most intense cores (intensity kCrystalLow to
// kCrystalHigh: the crystalline cores of the highest chains), from -kCharacterEdge to
// kCharacterEdge.
constexpr double kCharacterWavelength = 160'000.0;
constexpr double kWornStrata = 0.4;
constexpr double kSharp = 0.7;
constexpr double kIsolatedStrata = 0.6;
constexpr double kCrystalCores = 0.6;
constexpr float kCrystalLow = 0.6f;
constexpr float kCrystalHigh = 0.8f;
constexpr double kCharacterEdge = 0.15;

// The strata: great bands kBandLow to kBandHigh high (a noise of kBandWavelength), offset by up
// to kDip meters (a noise of kDipWavelength, 2 octaves: a dip of a few degrees); rock steps
// kStepShare of them high. Their rims recede by up to kRim meters, their faces rise over kRunLow
// to kRunHigh of their height (one noise of kRimWavelength: 75 to 88 degrees), their treads rise
// kTilt of their band.
constexpr double kBandLow = 100.0;
constexpr double kBandHigh = 400.0;
constexpr double kBandWavelength = 4'000.0;
constexpr double kDip = 1500.0;
constexpr double kDipWavelength = 20'000.0;
constexpr double kStepShare = 0.15;
constexpr double kStepMask = 0.5;
constexpr double kRim = 25.0;
constexpr double kRunLow = 0.05;
constexpr double kRunHigh = 0.25;
constexpr double kRimWavelength = 300.0;
constexpr double kTilt = 0.25;
// The share of the layers that are hard (a ledge and a wall), the others soft (the slope goes
// on): for the great bands, the rock steps and the joints.
constexpr double kHardBands = 0.7;
constexpr double kHardSteps = 0.7;
constexpr double kHardJoints = 0.85;
// The treads of the joints keep kJointTilt of the slope of the ground: no plane slabs.
constexpr double kJointTilt = 0.55;
// The share of a band its face may take at most.
constexpr double kFaceShare = 0.3;
// The slope of the ground is read at least as kLeastSlope.
constexpr double kLeastSlope = 0.05;

// The crystalline rock: cells of kCellSize meters at each scale, cut by joints kJointLow to
// kJointHigh meters apart along planes of slope kTiltLow to kTiltHigh (20 to 50 degrees); the
// tops blend over kRamp meters across the fractures; the finer scale counts kFine of the coarser.
constexpr std::array<double, 2> kCellSize = {512.0, 128.0};
constexpr std::array<double, 2> kJointLow = {80.0, 15.0};
constexpr std::array<double, 2> kJointHigh = {260.0, 55.0};
constexpr double kTiltLow = 0.36;
constexpr double kTiltHigh = 1.2;
constexpr double kRamp = 2.0;
constexpr double kFine = 0.5;
// The joints wave by up to kJointWave of their spacing (the noise of the rims): never a row of
// even teeth.
constexpr double kJointWave = 0.6;
constexpr double kCellJitter = 0.4;
// Cells whose weight is below exp(-kFarWeight) are left out.
constexpr double kFarWeight = 16.0;

noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, int count, double wavelength,
                          const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = count,
                              .frequency = 1.0 / wavelength,
                              .lattices = noise::OctaveLattices::RotatedFast},
                             world);
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double fbm(const noise::FbmOctaves& n, double x, double z, noise::FbmOctaves::Memo* memo) {
    return static_cast<double>(memo != nullptr ? n.fbm2D(x, z, *memo) : n.fbm2D(x, z));
}

const Cells& cellsAt(const Noises& n, int scale, int i, int j, Cells& cells) {
    if (cells.valid && cells.i == i && cells.j == j) {
        return cells;
    }
    const auto size = kCellSize[static_cast<std::size_t>(scale)];
    const auto countX = std::max(1, static_cast<int>(std::lround(n.world.x / size)));
    const auto countZ = std::max(1, static_cast<int>(std::lround(n.world.z / size)));
    const std::uint64_t seed = noise::deriveSeed(n.crystal, static_cast<std::uint64_t>(scale));
    for (std::size_t k = 0; k < cells.cells.size(); ++k) {
        const int di = static_cast<int>(k % 3) - 1;
        const int dj = static_cast<int>(k / 3) - 1;
        const auto ci = static_cast<std::uint64_t>(((i + di) % countX + countX) % countX);
        const auto cj = static_cast<std::uint64_t>(((j + dj) % countZ + countZ) % countZ);
        const std::uint64_t hash =
            noise::deriveSeed(seed, cj * static_cast<std::uint64_t>(countX) + ci);
        Cell& c = cells.cells[k];
        c.x = di + 0.5 + kCellJitter * (2.0 * field(hash, 0) - 1.0);
        c.z = dj + 0.5 + kCellJitter * (2.0 * field(hash, 1) - 1.0);
        const auto s = static_cast<std::size_t>(scale);
        const double tilt = kTiltLow + (kTiltHigh - kTiltLow) * field(hash, 2);
        const double angle = field(hash, 3);
        c.tiltX = tilt * dmath::cosTurns(angle);
        c.tiltZ = tilt * dmath::sinTurns(angle);
        const std::uint64_t more = noise::deriveSeed(hash, 1);
        c.band = kJointLow[s] + (kJointHigh[s] - kJointLow[s]) * field(more, 0);
        c.offset = c.band * field(more, 1);
        c.layers = noise::deriveSeed(more, 2);
    }
    cells.valid = true;
    cells.i = i;
    cells.j = j;
    return cells;
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    Noises n;
    n.character = octaves(seed, 0, 1, kCharacterWavelength, world);
    n.isolated = octaves(seed, 1, 1, kIsolatedWavelength, world);
    n.patches = octaves(seed, 2, 1, kPatchWavelength, world);
    n.bands = octaves(seed, 3, 1, kBandWavelength, world);
    n.dip = octaves(seed, 4, 2, kDipWavelength, world);
    n.rims = octaves(seed, 5, 2, kRimWavelength, world);
    n.crystal = noise::deriveSeed(seed, 6);
    n.layers = noise::deriveSeed(seed, 7);
    n.steps = noise::deriveSeed(seed, 8);
    n.world = world;
    return n;
}

Masks masks(const Noises& n, const Column& c, double x, double z, Memo* memo) {
    const double slope = dmath::hypot(c.gradX, c.gradZ);
    const double open = (1.0 - static_cast<double>(c.floor)) *
                        (1.0 - static_cast<double>(c.crest)) *
                        smooth(kReliefLow, kReliefHigh, static_cast<double>(c.relief));
    const double core = static_cast<double>(noise::smoothstep(kCoreLow, kCoreHigh, c.intensity)) *
                        smooth(kSteepLow, kSteepHigh, slope);
    const double foothills =
        static_cast<double>(noise::smoothstep(kFoothillsLow, kFoothillsHigh, c.intensity)) *
        smooth(kGentleLow, kGentleHigh, slope);
    Masks out;
    if (!(open > 0.0) || !(core > 0.0 || foothills > 0.0)) {
        return out;
    }
    const double isolated =
        foothills > 0.0
            ? smooth(kIsolatedLow, kIsolatedHigh,
                     fbm(n.isolated, x, z, memo ? &memo->isolated : nullptr))
            : 0.0;
    const double wall = std::max(core, foothills * isolated) * open;
    if (!(wall > 0.0)) {
        return out;
    }
    const double patches =
        smooth(kPatchLow, kPatchHigh, fbm(n.patches, x, z, memo ? &memo->patches : nullptr));
    const double mask = wall * (kPatchFloor + (1.0 - kPatchFloor) * patches);
    const double layered =
        smooth(-kCharacterEdge, kCharacterEdge,
               fbm(n.character, x, z, memo ? &memo->character : nullptr) +
                   kWornStrata * (kSharp - static_cast<double>(c.character)) +
                   kIsolatedStrata * isolated -
                   kCrystalCores * static_cast<double>(
                                       noise::smoothstep(kCrystalLow, kCrystalHigh, c.intensity)));
    out.strata = mask * layered;
    out.crystal = mask * (1.0 - layered);
    return out;
}

double strataTop(double ground, double slope, double band, double offset, double rim, double run,
                 double tilt, std::uint64_t layers, double hard) {
    const double s = std::max(slope, kLeastSlope);
    const double u = (ground - rim * s - offset) / band;
    const double k = std::floor(u);
    const double t = u - k;
    // A soft layer keeps the slope; a hard one is a ledge, then a wall up to the next layer.
    const auto index = static_cast<std::uint64_t>(static_cast<std::int64_t>(k));
    if (!(field(noise::deriveSeed(layers, index), 0) < hard)) {
        return offset + band * u;
    }
    // The face takes the share of the band the ground climbs over run meters.
    const double face = std::clamp(s * run / band, 0.0, kFaceShare);
    const double riser = face > 0.0 ? smooth(1.0 - face, 1.0, t) : (t >= 1.0 ? 1.0 : 0.0);
    return offset + band * (k + tilt * t + (1.0 - tilt) * riser);
}

double crystalTop(const Noises& n, double x, double z, double ground, double gradX,
                  double gradZ, int scale, double ramp, Memo* memo) {
    const auto s = static_cast<std::size_t>(scale);
    const double size = kCellSize[s];
    const double cx = (x + 0.5 * n.world.x) / size;
    const double cz = (z + 0.5 * n.world.z) / size;
    const auto i = static_cast<int>(std::floor(cx));
    const auto j = static_cast<int>(std::floor(cz));
    Cells local;
    const Cells& cells = cellsAt(n, scale, i, j, memo != nullptr ? memo->crystal[s] : local);
    const double px = cx - i;
    const double pz = cz - j;
    std::array<double, 9> distance2{};
    double nearest = 1.0e300;
    std::size_t first = 0;
    for (std::size_t k = 0; k < cells.cells.size(); ++k) {
        const double dx = px - cells.cells[k].x;
        const double dz = pz - cells.cells[k].z;
        distance2[k] = (dx * dx + dz * dz) * size * size;
        if (distance2[k] < nearest) {
            nearest = distance2[k];
            first = k;
        }
    }
    // The ground of a cell cut by its joints: strata along its tilted planes, waving.
    const double wave = kJointWave * fbm(n.rims, x, z, memo ? &memo->rims : nullptr);
    const auto cut = [&](std::size_t k) {
        const Cell& c = cells.cells[k];
        const double lift = (c.tiltX * (px - c.x) + c.tiltZ * (pz - c.z)) * size;
        const double slope = dmath::hypot(gradX + c.tiltX, gradZ + c.tiltZ);
        return strataTop(ground + lift, slope, c.band, c.offset + wave * c.band, 0.0,
                         c.band * (kRunLow + (kRunHigh - kRunLow) * 0.35), kJointTilt,
                         c.layers, kHardJoints) -
               lift;
    };
    if (!(ramp > 0.0)) {
        return cut(first);
    }
    const double spread = 2.0 * size * ramp;
    double sum = 0.0;
    double weights = 0.0;
    for (std::size_t k = 0; k < cells.cells.size(); ++k) {
        const double e = (distance2[k] - nearest) / spread;
        if (e > kFarWeight) {
            continue;
        }
        const double w = k == first ? 1.0 : dmath::exp(-e);
        sum += w * cut(k);
        weights += w;
    }
    return sum / weights;
}

double surface(const Noises& n, const Column& c, double x, double z, Memo* memo) {
    const Masks m = masks(n, c, x, z, memo);
    double h = c.ground;
    if (m.strata > 0.0) {
        const double slope = dmath::hypot(c.gradX, c.gradZ);
        const double band =
            kBandLow + (kBandHigh - kBandLow) *
                           smooth(-0.5, 0.5, fbm(n.bands, x, z, memo ? &memo->bands : nullptr));
        const double offset = kDip * fbm(n.dip, x, z, memo ? &memo->dip : nullptr);
        const double rims = 0.5 + 0.5 * fbm(n.rims, x, z, memo ? &memo->rims : nullptr);
        const double rim = kRim * rims;
        const double run = kRunLow + (kRunHigh - kRunLow) * (1.0 - rims);
        h += m.strata *
             (strataTop(h, slope, band, offset, rim, run * band, kTilt, n.layers, kHardBands) - h);
        const double steps = kStepShare * band;
        h += kStepMask * m.strata * (strataTop(h, slope, steps, 0.37 * offset, kStepShare * rim,
                                               run * steps, kTilt, n.steps, kHardSteps) -
                                     h);
    }
    if (m.crystal > 0.0) {
        h += m.crystal * (crystalTop(n, x, z, h, c.gradX, c.gradZ, 0, kRamp, memo) - h);
        h += kFine * m.crystal * (crystalTop(n, x, z, h, c.gradX, c.gradZ, 1, kRamp, memo) - h);
    }
    return h;
}

double density(const Noises& n, const Column& c, double x, double z, double y, Memo* memo) {
    return y - surface(n, c, x, z, memo);
}

} // namespace terrain::mountain_walls
