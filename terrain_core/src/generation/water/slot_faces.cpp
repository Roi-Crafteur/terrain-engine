#include "slot_faces.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cstdint>

namespace terrain::water {

namespace {

// The notches vary along the walls: shallower by kNotchSwing meters at most, higher or lower by
// kNotchRise, with a noise of the place (fbm2D on rotated lattices, waves of about kNotchWave
// meters: no row along any wall, nor the walls facing each other across a slot alike).
constexpr double kNotchSwing = 0.6;
constexpr double kNotchRise = 0.75;
constexpr double kNotchWave = 40.0;
constexpr std::uint64_t kNotchSeed = 0x6e6f'7463'6865'7331ULL;
constexpr noise::FbmParams kNotchNoise{3, 1.0 / kNotchWave, 2.17, 0.5,
                                       noise::OctaveLattices::Rotated};
// The notch stays kNotchBack meters short of the back of the run of its face (its rock).
constexpr double kNotchBack = 0.5;
// It is kNotchLeast meters high at least.
constexpr double kNotchLeast = 5.0;
// Its middle lies half its usual height above the foot of the wall.
constexpr double kNotchMiddle = 0.5 * kNotchHighSoft;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A noise of the place in [-1, 1], of stream k.
double placeNoise(double x, double z, std::uint64_t k, const WorldSize& world) {
    const double n = noise::fbm2D(x, z, noise::deriveSeed(kNotchSeed, k), kNotchNoise, world);
    return std::clamp(2.0 * n, -1.0, 1.0);
}

} // namespace

bool notched(const SlotWall& w) {
    return w.width >= kNotchWidth && static_cast<double>(w.top) - w.foot > kNotchFrom;
}

double notchHardnessHeight(const SlotWall& w) {
    return static_cast<double>(w.foot) + kNotchMiddle;
}

RockFace slotFace(const SlotWall& w, double x, double z, float hardness, const WorldSize& world) {
    RockFace f;
    f.weight = 0.0f;
    if (!(w.width > 0.0f) || !(w.weight > 0.0f) || !(w.top > w.foot)) {
        return f;
    }
    f.foot = w.foot;
    f.top = w.top;
    f.distance = w.distance;
    f.run = w.width;
    f.airX = w.airX;
    f.airZ = w.airZ;
    f.carved = true;
    f.weight = std::min(1.0f, w.weight);
    if (notched(w)) {
        const double hard = std::clamp(static_cast<double>(hardness), 0.0, 1.0);
        const double share =
            smooth(kNotchFrom, kNotchFull, static_cast<double>(w.top) - w.foot);
        const double shallower = 0.5 * kNotchSwing * (1.0 + placeNoise(x, z, 0, world));
        const double deep =
            std::clamp(lerp(kNotchDeepSoft, kNotchDeepHard, hard) - shallower, 0.0,
                       std::min(kNotchDeepSoft, 0.5 * w.width - kNotchBack));
        const double high = std::clamp(lerp(kNotchHighSoft, kNotchHighHard, hard) +
                                           kNotchRise * placeNoise(x, z, 1, world),
                                       kNotchLeast, kNotchHighSoft);
        f.undercut = static_cast<float>(share * deep);
        f.undercutHeight = static_cast<float>(high);
    }
    // Faded out on low walls (kLeastRockFace), as the faces of the relief.
    anchorFace(f, w.foot, w.top);
    return f;
}

} // namespace terrain::water
