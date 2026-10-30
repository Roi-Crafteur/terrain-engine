#include "coast_shape.hpp"

#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::coast_shape {

namespace {

// The coastal plain rises this many meters per meter from the shore, up to kCoastPlainTop,
// before the regional elevation takes over over GeographyParams::coastRamp.
constexpr double kCoastPlainRise = 0.002;
constexpr double kCoastPlainTop = 20.0;

// Streams and wavelengths of the noises of the shapes.
constexpr std::uint64_t kHeightStream = 0x33B1;
constexpr std::uint64_t kPlatformStream = 0x33B2;
constexpr std::uint64_t kFaceStream = 0x33B3;
constexpr double kHeightWavelength = 6'000.0;
constexpr double kPlatformWavelength = 1'700.0;
constexpr double kFaceWavelength = 900.0;

// The shape of the shore of a kind: the top of its face kHeightLow to kHeightHigh meters high by
// the height noise, at most landShare of the regional elevation plus landTop (no plateau above
// its hinterland); a rock platform platformLow to platformHigh meters wide before a face faceLow
// to faceHigh meters wide; its hills (the factor of the land) faded in over hills meters of signal;
// out at sea its floor deepen meters deeper than the shelf within foot meters of the shore.
struct Profile {
    float heightLow;
    float heightHigh;
    float landShare;
    float landTop;
    float platformLow;
    float platformHigh;
    float faceLow;
    float faceHigh;
    double hills;
    float deepen;
    double foot;
};

// By kind, in the order of CoastKind (the low coasts: the coastal plain, unused).
constexpr std::array<Profile, kCoastKindCount> kProfiles{{
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30'000.0, 0.0f, 1.0},
    {3.0f, 14.0f, 0.5f, 6.0f, 5.0f, 40.0f, 2.0f, 5.0f, 12'000.0, 6.0f, 300.0},
    {20.0f, 180.0f, 0.8f, 25.0f, 10.0f, 120.0f, 4.0f, 10.0f, 15'000.0, 15.0f, 150.0},
    {40.0f, 200.0f, 0.9f, 40.0f, 0.0f, 10.0f, 3.0f, 6.0f, 10'000.0, 25.0f, 60.0},
    {10.0f, 50.0f, 0.8f, 15.0f, 0.0f, 5.0f, 40.0f, 120.0f, 10'000.0, 8.0f, 200.0},
    {8.0f, 30.0f, 1.0f, 8.0f, 0.0f, 5.0f, 3.0f, 8.0f, 5'000.0, 60.0f, 600.0},
    {10.0f, 40.0f, 1.0f, 10.0f, 0.0f, 5.0f, 3.0f, 8.0f, 5'000.0, 90.0f, 600.0},
}};

// The rock platform rises from kBenchLow to kBenchLow + kBenchRise meters across its width, from
// the water over its first kBenchFoot meters.
constexpr double kBenchLow = 0.4;
constexpr double kBenchRise = 1.1;
constexpr double kBenchFoot = 1.5;
// The top of a face gives way to the ground of its hinterland from kTopLow to kTopHigh meters of
// signal; the floor comes back to the shelf from kSeaBack to kCoastSeaBand of signal at sea.
constexpr double kTopLow = 5'000.0;
constexpr double kTopHigh = 25'000.0;
constexpr double kSeaBack = 10'000.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

noise::FbmOctaves shapeNoise(std::uint64_t seed, std::uint64_t stream, double wavelength,
                             const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = 3, .frequency = 1.0 / wavelength, .gain = 0.5}, world);
}

// A noise in [0, 1].
double unit(const noise::FbmOctaves& n, double x, double z) {
    return std::clamp(0.5 + 0.5 * static_cast<double>(n.fbm2D(x, z)), 0.0, 1.0);
}

// The ground of a shore of profile k at distance d from the water (d > 0): its platform, then its
// face up to top.
double shoreGround(const Profile& k, double top, double d, double platformAt, double faceAt) {
    const double platform =
        static_cast<double>(k.platformLow) +
        static_cast<double>(k.platformHigh - k.platformLow) * platformAt;
    const double face =
        static_cast<double>(k.faceLow) + static_cast<double>(k.faceHigh - k.faceLow) * faceAt;
    const double rise = platform > 0.0 ? kBenchRise * std::min(d, platform) / platform : 0.0;
    const double bench = platform > 0.0 ? (kBenchLow + rise) * smooth(0.0, kBenchFoot, d) : 0.0;
    return bench + (top - bench) * smooth(platform, platform + face, d);
}

} // namespace

bool inBand(const GeographyParams& p, double coast) {
    return coast > -kCoastSeaBand && coast < p.coastRamp;
}

float landFactor(const GeographyParams& p, double coast) {
    return noise::smoothstep(0.0f, static_cast<float>(p.coastRamp), static_cast<float>(coast));
}

double shelf(const GeographyParams& p, double coast) {
    const double d = -coast;
    const double depth = static_cast<double>(p.shelfDepth);
    return d <= p.shelfWidth ? depth * d / p.shelfWidth
                             : depth - (d - p.shelfWidth) * p.shelfSlope;
}

float groundBase(const GeographyParams& p, const GeographySample& g, double coast) {
    if (coast < 0.0) {
        return std::max(g.sea, static_cast<float>(shelf(p, coast)));
    }
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double t = landFactor(p, coast);
    return static_cast<float>(plain + (static_cast<double>(g.land) - plain) * t);
}

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    return {shapeNoise(seed, kHeightStream, kHeightWavelength, world),
            shapeNoise(seed, kPlatformStream, kPlatformWavelength, world),
            shapeNoise(seed, kFaceStream, kFaceWavelength, world)};
}

namespace {

struct Shore {
    double heightAt = 0.0; // the noises at the point, in [0, 1]
    double platformAt = 0.0;
    double faceAt = 0.0;
};

// The land factor and the ground of a kind on land (coast > 0).
Ground landOf(const GeographyParams& p, CoastKind kind, const GeographySample& g, double coast,
              double d, const Shore& shore) {
    const double old = groundBase(p, g, coast);
    if (kind == CoastKind::Low) {
        return {landFactor(p, coast), static_cast<float>(old)};
    }
    const Profile& k = kProfiles[static_cast<std::size_t>(kind)];
    const bool mountains = kind == CoastKind::Mountain || kind == CoastKind::Fjord;
    // The mountains rise from the shore within k.hills of signal, the regional elevation too.
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double hinterland =
        mountains ? plain + (static_cast<double>(g.land) - plain) * smooth(0.0, k.hills, coast)
                  : old;
    const double high = static_cast<double>(k.heightLow) +
                        static_cast<double>(k.heightHigh - k.heightLow) * shore.heightAt;
    const double cap =
        static_cast<double>(k.landShare) * std::max(0.0, static_cast<double>(g.land)) +
        static_cast<double>(k.landTop);
    const double h = std::min(high, cap);
    const double top = h + (hinterland - h) * smooth(kTopLow, kTopHigh, coast);
    const double ground =
        std::max(hinterland, shoreGround(k, top, d, shore.platformAt, shore.faceAt));
    return {static_cast<float>(smooth(0.0, k.hills, coast)), static_cast<float>(ground)};
}

// The floor of a kind at sea (coast < 0): the shelf, deepened at the foot of the shore.
double seaOf(const GeographyParams& p, CoastKind kind, const GeographySample& g, double coast,
             double d) {
    const Profile& k = kProfiles[static_cast<std::size_t>(kind)];
    const double deepen = static_cast<double>(k.deepen) * smooth(0.0, k.foot, -d) *
                          (1.0 - smooth(kSeaBack, kCoastSeaBand, -coast));
    return std::max(static_cast<double>(g.sea), shelf(p, coast) - deepen);
}

} // namespace

Ground shapeAt(const GeographyParams& p, const Noises& noises, double x, double z,
               const GeographySample& g, double coast, double distance,
               const coast_kinds::Weights& weights) {
    if (coast < 0.0) {
        double ground = 0.0;
        for (std::size_t k = 0; k < weights.size(); ++k) {
            if (weights[k] > 0.0f) {
                ground += static_cast<double>(weights[k]) *
                          seaOf(p, static_cast<CoastKind>(k), g, coast, distance);
            }
        }
        return {0.0f, static_cast<float>(ground)};
    }
    const Shore shore{unit(noises.height, x, z), unit(noises.platform, x, z),
                      unit(noises.face, x, z)};
    double land = 0.0;
    double ground = 0.0;
    for (std::size_t k = 0; k < weights.size(); ++k) {
        if (weights[k] > 0.0f) {
            const Ground kind = landOf(p, static_cast<CoastKind>(k), g, coast, distance, shore);
            land += static_cast<double>(weights[k]) * static_cast<double>(kind.land);
            ground += static_cast<double>(weights[k]) * static_cast<double>(kind.ground);
        }
    }
    return {static_cast<float>(land), static_cast<float>(ground)};
}

} // namespace terrain::coast_shape
