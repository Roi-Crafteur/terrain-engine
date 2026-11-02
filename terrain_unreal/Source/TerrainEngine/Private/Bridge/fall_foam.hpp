#pragma once

#include "fall_edges.hpp"

#include "terrain/streaming/stream_manager.hpp"

#include <array>

namespace terrain::unreal {

// The foam at the foot of a fall (buildFallCurtains), an ellipse on its pool along the foot of
// its curtain: across the fall, the foot of the water falling (curtainRuns: from wetFrom to
// wetTo, or the band of a straight lip) and kFoamSideShare of the width of its channel on each
// side; downstream, as long at most, kFoamRadiusShare of that width (kFoamLeastRadius at least,
// within its pool: TileFall::pool), its center kFoamAheadShare of that downstream of the middle
// of the foot. Its ragged rim (kFoamRim of its semi-axes in the material: 1 - smoothstep(0.3, 1,
// r + 0.25 (n - 0.5)) is 0 from r = 1.125 on, its noise n in [0, 1]) stays kFoamClear meters
// short of the edge of the lip of each fall its pool runs to (whose water above stands at the
// water below it): shorter downstream first, then narrower if that lip comes from a side. Only
// where every tile the foam may reach knows that fall (its lip within kFallNear meters of them
// along x and z): the same foam in every tile.
inline constexpr double kFoamSideShare = 0.15;
inline constexpr double kFoamRim = 1.125;
inline constexpr double kFoamClear = 2.0;

struct FoamShape {
    double centerX = 0.0; // frame of the tile
    double centerZ = 0.0;
    double alongX = 1.0; // unit along the foot of the curtain
    double alongZ = 0.0;
    double along = 0.0;  // semi-axis along the foot, meters
    double down = 0.0;   // semi-axis across it, downstream: the radius the material reads (uv2.v)
    double footHalf = 0.0; // half the length of the foot of the water falling
};

// The foam of fall f of the tile of data (f one of data.falls; edges: FallEdges(data)).
[[nodiscard]] FoamShape foamShape(const TileFall& f, const TileData& data, const FallEdges& edges);

// The offset of (x, z) (frame of the tile) from the center of the foam, its part along the foot
// scaled by down / along, along x and z: its length over down is 1 on the ellipse (uv0).
[[nodiscard]] std::array<double, 2> foamOffset(const FoamShape& s, double x, double z);

} // namespace terrain::unreal
