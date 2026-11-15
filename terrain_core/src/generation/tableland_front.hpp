#pragma once

// The fronts of the provinces of the plateaus and the karst (agent 46, tablelands.hpp), internal to
// terrain_core: the share of a province from its potential (read on the cells of the pre-climate at
// the warped point), faded from the coast, its front moved across itself.
//
// The potentials vary slowly (the regional share of layered rock over 3 x 3 cells of 65 km, a
// supply of 900 km): their contours run straight or in wide arcs over a hundred kilometers and
// more, and the warps of the points bend them by the deviation of their noises, about a fifth of
// their amplitudes (a noise may moreover stay nearly even along a whole front). The front is moved
// besides along its normal by a swell: three waves of kShiftWavelength meters, kShift meters each,
// sixty degrees apart (along any front two of them at least rise and fall), their phase twisted by
// up to kTwist of a turn by a noise of kTwistWavelength (no period), whole numbers of them on the
// world (no seam). The potential is lifted by that shift times its gradient (read by differences
// over kStep meters), at most kLiftMost: its line stays a graph over the contour (no fold), nothing
// rises where the potential is flat, and nothing is moved farther than its gradient carries it.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain::tableland_front {

// A province from kShareLow to kShareHigh of its potential, its edge moved by up to kEdge of it (a
// noise of the caller).
inline constexpr float kShareLow = 0.35f;
inline constexpr float kShareHigh = 0.6f;
inline constexpr float kEdge = 0.12f;
// None within kCoastNear meters of the coast of the geography, full from kCoastFar (the coasts are
// the band of agent 33, 30 km inland at most). The fade reads the distance to the coast less up to
// kCoastWobble meters (the warps of the edges: never more, so nothing comes nearer than kCoastNear).
inline constexpr double kCoastNear = 60'000.0;
inline constexpr double kCoastFar = 150'000.0;
inline constexpr double kCoastWobble = 30'000.0;
// The shift of the fronts (see above), and the step of the differences of the gradient.
inline constexpr double kShift = 20'000.0;
inline constexpr double kShiftWavelength = 150'000.0;
inline constexpr double kTwist = 0.3;
inline constexpr double kTwistWavelength = 400'000.0;
inline constexpr double kStep = 20'000.0;
inline constexpr double kLiftMost = 0.8;

// The fade of the provinces from the coast, coast meters away, of wobble wobble (in [-1, 1]).
[[nodiscard]] float coastFade(double coast, double wobble);

// The share of a province of potential potential lifted by lift, its edge moved by the noise edge
// at (x, z), faded by coast.
[[nodiscard]] float share(float potential, float lift, const noise::FbmOctaves& edge, double x,
                          double z, float coast);

// The swell of the fronts: the noise of the twist of its phase, the wave numbers of its waves (whole
// numbers of them on the world, per meter).
struct Swell {
    noise::FbmOctaves twist;
    std::array<double, 3> kx{};
    std::array<double, 3> kz{};
};
[[nodiscard]] Swell makeSwell(std::uint64_t seed, const WorldSize& world);

// The shift of the fronts at (x, z), meters (at most 3 kShift).
[[nodiscard]] double shiftAt(const Swell& swell, double x, double z);

// The bends of the escarpments at the scale of their stretches of 20 km (tableland_relief.cpp):
// the same swell, its waves kBendWavelength meters long, kBendShift meters each, their phase twisted
// by up to kBendTwist of a turn by a noise of kBendTwistWavelength (along any rim two of them at
// least rise and fall, where the noise of the bends may stay even over a whole rim).
inline constexpr double kBendShift = 1'500.0;
inline constexpr double kBendWavelength = 30'000.0;
inline constexpr double kBendTwist = 0.3;
inline constexpr double kBendTwistWavelength = 120'000.0;
[[nodiscard]] Swell makeBendSwell(std::uint64_t seed, const WorldSize& world);

// The bend of the escarpments at (x, z), meters (at most 3 kBendShift).
[[nodiscard]] double bendAt(const Swell& swell, double x, double z);

// The lift of a potential at a point shifted by shift meters, the potential read kStep meters
// east, west, north and south of it.
[[nodiscard]] double liftOf(double shift, double east, double west, double north, double south);

// The most a potential may reach around a cell (lifted), its cells reading at most most and
// differing by difference at most from their neighbors, cellSize meters apart.
[[nodiscard]] double mostLifted(double most, double difference, double cellSize);

} // namespace terrain::tableland_front
