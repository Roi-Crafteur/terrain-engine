#include "coast_profile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::coast_profile {

namespace {

// By kind, in the order of CoastKind.
constexpr std::array<Profile, kCoastKindCount> kProfiles{{
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30'000.0, 0.0f, 1.0},
    {2.0f, 14.0f, 0.5f, 6.0f, 5.0f, 40.0f, 0.0f, 6.0f, 0.0f, 12'000.0, 0.5f, 300.0},
    {4.0f, 180.0f, 0.8f, 25.0f, 10.0f, 120.0f, 0.0f, 10.0f, 0.8f, 15'000.0, 0.8f, 200.0},
    {8.0f, 200.0f, 0.9f, 40.0f, 0.0f, 10.0f, 0.0f, 10.0f, 0.6f, 10'000.0, 1.0f, 100.0},
    {4.0f, 50.0f, 0.8f, 15.0f, 0.0f, 5.0f, 40.0f, 120.0f, 0.0f, 10'000.0, 0.5f, 300.0},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, 1.0f, 150.0},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, 1.0f, 120.0},
}};

// The height of the faces: the slow noise, contrasted from kStretchLow to kStretchHigh, times
// kMediumLow to 1 by the medium one: from a few meters to the highest along the same coast.
constexpr double kStretchLow = -0.45;
constexpr double kStretchHigh = 0.45;
constexpr double kMediumLow = 0.3;
// The screes stand where their noise passes kScreeLow to kScreeHigh (coast_shape.cpp).
constexpr double kScreeLow = 0.45;
constexpr double kScreeHigh = 0.75;

} // namespace

const Profile& profileOf(CoastKind kind) {
    return kProfiles[static_cast<std::size_t>(kind)];
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double lerp(float low, float high, double t) {
    return static_cast<double>(low) + static_cast<double>(high - low) * t;
}

double unit(const noise::FbmOctaves& n, double x, double z) {
    return std::clamp(0.5 + 0.5 * static_cast<double>(n.fbm2D(x, z)), 0.0, 1.0);
}

Shore shoreAt(const coast_shape::Noises& n, double x, double z) {
    const double stretch =
        smooth(kStretchLow, kStretchHigh, static_cast<double>(n.stretch.fbm2D(x, z)));
    return {stretch * (kMediumLow + (1.0 - kMediumLow) * unit(n.height, x, z)),
            smooth(0.2, 0.8, unit(n.platform, x, z)), unit(n.face, x, z),
            2.0 * smooth(0.2, 0.8, unit(n.edge, x, z)) - 1.0,
            smooth(kScreeLow, kScreeHigh, unit(n.scree, x, z))};
}

Face faceAt(const Profile& k, double top, double d, const Shore& s) {
    Face f;
    f.foot = std::max(0.0, lerp(k.platformLow, k.platformHigh, s.platformAt) +
                               kEdgeShift * s.edgeAt);
    const double rise = kBenchRise * std::min(d, f.foot) / std::max(f.foot, kBenchRun);
    f.bench = (kBenchLow + rise) * smooth(0.0, kBenchFoot, d);
    f.height = std::max(0.0, top - f.bench);
    f.width = kDistanceSlack * kSmoothSlope * f.height / coast_shape::faceSlopeLimit(f.height) +
              lerp(k.faceLow, k.faceHigh, s.faceAt);
    f.width = std::max(f.width, kBenchFoot);
    return f;
}

double faceTop(const Profile& k, const Shore& s, double land) {
    const double high = lerp(k.heightLow, k.heightHigh, s.heightAt);
    const double cap = static_cast<double>(k.landShare) * std::max(0.0, land) +
                       static_cast<double>(k.landTop);
    return std::min(high, cap);
}

} // namespace terrain::coast_profile
