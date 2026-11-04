// The sand of the ergs over the relief of the geography (ergs.hpp: Ergs::dunesAt): the local
// relief it buries, the rock that stands out of it and the share of the surface it covers there.
#include "terrain/generation/ergs.hpp"

#include "dune_waves.hpp"
#include "ergs_impl.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>

namespace terrain {

namespace {

// The far horizon: the spacing of the dunes estimated from the sand (kMeanLeast + kMeanRange
// sand^1.3, as the pivots), the dunes giving way to their mean from kFilterLow to kFilterHigh of
// it.
constexpr double kMeanLeast = 1000.0;
constexpr double kMeanRange = 1900.0;
constexpr double kFilterLow = 0.1;
constexpr double kFilterHigh = 0.3;
constexpr double kBuryMost = 60.0; // meters of the local relief the sand buries (buried())
constexpr float kBuryFade = 0.3f;
constexpr double kRockFall = 5.0;
// A hollow keeps this share of what it sinks under kBuryMost: the sand lines its walls, gentler.
constexpr double kHollowKept = 0.35;
// The sand covers the rock out of it up to kFootLow to kFootLow + kFootRange meters over its
// own surface (the foot noise of the ergs, tens of meters), then gives way to the rock over
// kFootBlend meters: no hard edge, and the sand climbs the foot irregularly.
constexpr double kFootLow = 2.0;
constexpr double kFootRange = 18.0;
constexpr double kFootBlend = 8.0;

// max(0, d), its slope and curvature continuous over [-k, k].
double softPositive(double d, double k) {
    if (d <= -k) {
        return 0.0;
    }
    return d >= k ? d : (d + k) * (d + k) * (d + k) * (3.0 * k - d) / (16.0 * k * k * k);
}

double hermite(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The sand of the ergs buries the local relief of the geography (its hills, buttes and plains:
// HeightSample::base and detail over regional): the surface of the dunes rides on the regional
// ground, a hollow keeps kHollowKept of what it sinks under it past kBuryMost; the rock of a butte
// (lowered by kBuryMost, deep under the plains) stands out of the sand where it rises over it
// (its slopes never add to those of the dunes). Faded with the mask over its first kBuryFade:
// none at the edges of the erg. sand: the share of the surface the sand covers (cover(): the
// meters it climbs up the rock there, read only where the rock rises over the sand).
template <typename Cover>
float buried(const HeightSample& s, const ErgPoint& e, double dunes, const Cover& cover,
             float* sand) {
    // With its fine detail (a few meters of noise: on the slip faces of the dunes it added 3
    // degrees, f9c3391).
    const double local = static_cast<double>(s.base) + static_cast<double>(s.detail) -
                         static_cast<double>(s.regional);
    if (local == 0.0) {
        return static_cast<float>(dunes);
    }
    const double hollow = kHollowKept * std::min(0.0, local + kBuryMost);
    const double share = static_cast<double>(noise::smoothstep(0.0f, kBuryFade, e.mask));
    // The rock: kBuryMost under the top of a butte, kRockFall times as deep under lower ground
    // (under the troughs of the tallest waves on a plain).
    const double rock = (local - kBuryMost) * (local > kBuryMost ? 1.0 : kRockFall);
    // The sand drapes the rock up to cover() meters over its own surface (its foot climbs the
    // rock irregularly): the rock shows past it only, cover() lower, joined to the sand over
    // kFootBlend meters with a continuous slope and curvature, and takes its color as soon as it
    // starts to show (never a face of rock, as steep as it is, in the color of the sand).
    const double sandTop = dunes + hollow - local;
    const double out = rock - (dunes + hollow); // meters of rock over the sand
    double surface = sandTop;
    if (out > kFootLow - kFootBlend) {
        const double c = cover();
        surface += softPositive(out - c, kFootBlend);
        if (sand != nullptr && share > 0.0) {
            *sand = static_cast<float>(1.0 - share * hermite(c - kFootBlend, c, out));
        }
    }
    return static_cast<float>(dunes + (surface - dunes) * share);
}

// The floor of the corridors between the dunes (HeightSample::ergFloor): sand sheets where the
// sand is plenty (its share over kSandyLow to kSandyHigh), gravel (a reg) where it is scarce,
// crusts (dry pans) in patches; the floors noise moves the border of the sand by up to
// kFloorNoise of its share, and lays the crusts where it rises from kCrustLow to kCrustHigh
// (within the gravel, patches kilometers wide, never following the relief: its buttes and its
// cliffs would cut them sharply).
constexpr double kSandyLow = 0.3;
constexpr double kSandyHigh = 0.75;
constexpr double kFloorNoise = 1.2;
constexpr double kCrustLow = 0.45;
constexpr double kCrustHigh = 0.6;
// Read where the dunes are less than kFloorReach meters deep only (deeper, the sand of the dunes
// rules the materials however wide the noise of the borders makes their bands: duneSand and 5
// times duneSandWidth, 9 m).
constexpr float kFloorReach = 10.0f;

float floorKind(const Ergs::Impl& m, double x, double z, const ErgPoint& e) {
    const double n = static_cast<double>(m.floors.fbm2D(x, z, memosOf(m).floors));
    const double sandy =
        hermite(kSandyLow, kSandyHigh, static_cast<double>(e.sand) - kFloorNoise * n);
    const double crusty = hermite(kCrustLow, kCrustHigh, n);
    const double stony = 1.0 - sandy;
    return static_cast<float>(stony + (2.0 - stony) * crusty);
}

} // namespace

float Ergs::dunesAt(double x, double z, const HeightSample& s, double footprint, float* mask,
                    float* sand, float* floor) const {
    if (mask != nullptr) {
        *mask = 0.0f;
    }
    if (sand != nullptr) {
        *sand = 1.0f;
    }
    if (floor != nullptr) {
        *floor = 0.0f;
    }
    if (s.water == WaterKind::Ocean || !mayHold(x, z)) {
        return 0.0f;
    }
    const ErgPoint e = at(x, z, s.relief, s.rift);
    if (!(e.mask > 0.0f)) {
        return 0.0f;
    }
    if (mask != nullptr) {
        *mask = e.mask;
    }
    // How high the sand climbs the rock there.
    const auto cover = [&] {
        const float n = impl_->foot.fbm2D(x, z, memosOf(*impl_).foot);
        return kFootLow + kFootRange * static_cast<double>(noise::smoothstep(-0.6f, 0.6f, n));
    };
    // The spacing of the dunes the sand gives (dune_phase: that of the pivots, smooth).
    // The waves of the ergs of those characters stand farther apart: their filter starts later.
    const double spacing =
        (kMeanLeast + kMeanRange * dmath::pow(static_cast<double>(e.sand), 1.3)) *
        (1.0 + static_cast<double>(e.waves) * (dune_waves::spacingScale(false) - 1.0) +
         static_cast<double>(e.giants) * (dune_waves::spacingScale(true) - 1.0));
    const double filtered = hermite(kFilterLow * spacing, kFilterHigh * spacing, footprint);
    const double point =
        filtered < 1.0 ? static_cast<double>(dune_field::dunes(*impl_, x, z, e, footprint)) : 0.0;
    // Their mean over their pattern (closed forms).
    const float out = filtered > 0.0
                          ? buried(s, e, point + (dune_field::meanDunes(*impl_, x, z, e) - point) *
                                                     filtered, cover, sand)
                          : buried(s, e, point, cover, sand);
    if (floor != nullptr && out < kFloorReach) {
        *floor = floorKind(*impl_, x, z, e);
    }
    return out;
}

} // namespace terrain
