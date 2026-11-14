#pragma once

// The small tools the forms of the plateaus share (agent 46: tableland_relief.cpp,
// tableland_canyons.cpp), internal to terrain_core: smoothsteps in doubles, the blend of the dry and
// humid styles, the signed distance to the zero line of a noise and the noises wrapped onto the
// world. Pure arithmetic of doubles (std::sqrt through dmath::hypot only): the same bits on every
// platform.

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::tableland_relief {

inline double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The value of a style between the humid lands (share 0) and the dry ones (share 1).
inline double mix(double humid, double arid, double share) {
    return humid + (arid - humid) * share;
}

// A signed distance in meters (positive within) and the unit gradient of its field.
struct Field {
    double value = 0.0;
    double dirX = 1.0;
    double dirZ = 0.0;
};

// The gradients of the fields are no smaller than this (per meter).
inline constexpr double kLeastGradient = 1e-9;

// The distance to the zero line of f (a field of the plane) at (x, z): its value over its
// gradient, by differences step meters long, the gradient read no smaller than least (where two
// zero lines cross, a saddle of the noise, it vanishes: the distance would blow up there).
template <typename F>
Field distanceOf(const F& f, double x, double z, double step, double least = kLeastGradient) {
    const double v = f(x, z);
    const double gradX = (f(x + step, z) - v) / step;
    const double gradZ = (f(x, z + step) - v) / step;
    const double g = dmath::hypot(gradX, gradZ);
    const double read = std::max(g, least);
    const double norm = std::max(g, kLeastGradient);
    return {v / read, gradX / norm, gradZ / norm};
}

// The noise f at (x, z), wrapped onto the world.
inline double wrapped(const noise::FbmOctaves& f, const WorldSize& w, double x, double z) {
    return static_cast<double>(
        f.fbm2D(noise::wrapCanonical(x, w.x), noise::wrapCanonical(z, w.z)));
}

// A layer of hard rock forced into a wall at a column: its top in meters under the top of the
// plateau, its height; a caprock holds up to the top, the great cliff of a canyon gives way above its
// top.
struct Unit {
    double top = 0.0;
    double height = 0.0; // 0: none
    bool capped = true;
};

// The forced hard layer of a wall tops this many meters under the top it holds (its top surface
// lies on the layer above it, lowered so much).
inline constexpr double kUnderTop = 3.0;

// A profile of two storeys at a signed distance distance to its outline (positive within): a cliff
// cliff meters high at the slope steep (its batter, tableland_cliffs.hpp: the hard layers laid over
// it make it a stack of vertical faces), then a concave talus at the mean slope talus down to the
// low ground, height meters under the top. share: of the height held there, in [0, 1]; rate: how fast
// it grows per meter toward the outline; cliff: on the cliff.
struct Storeys {
    double share = 0.0;
    double rate = 0.0;
    bool cliff = false;
};
inline Storeys storeysAt(double distance, double height, double cliff, double talus,
                         double steep) {
    if (!(height > 0.0)) {
        return {};
    }
    if (!(distance < 0.0)) {
        return {1.0, 0.0, false};
    }
    const double tall = std::clamp(cliff, 0.0, height);
    const double wall = tall / steep;
    const double run = (height - tall) / talus;
    const double d = -distance;
    if (d < wall) {
        return {1.0 - (tall / height) * (d / wall), (tall / height) / wall, true};
    }
    if (!(run > 0.0) || !(d < wall + run)) {
        return {};
    }
    const double v = (wall + run - d) / run; // 1 at the foot of the cliff, 0 at that of the talus
    const double below = 1.0 - tall / height;
    const double root = std::sqrt(std::max(v, 0.0));
    return {below * v * root, below * 1.5 * root / run, false};
}

} // namespace terrain::tableland_relief
