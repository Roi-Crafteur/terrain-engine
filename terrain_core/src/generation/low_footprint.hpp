#pragma once

// The footprint of a low of agent 52, internal to terrain_core: the points a site is accepted on,
// and the zones of the other agents no low may touch.
//
// The rule with agent 46 (plateaus, mesas, karst and their surfaces of plateau): a site of center
// (x, z) and reach r is refused if the zone of the other agent holds a point of its footprint: its
// center and rings at r / 2 and r, a point every 45 degrees. The second of the two branches merged
// wires the API of the other: Exclusion is Heightfield::tablelandAt(x, z).any() once agent 46 is
// on main (Heightfield::makeWater), empty until then.

#include "terrain/det_math.hpp"

#include <array>
#include <functional>

namespace terrain::lows {

// True where no low may lie; empty: nowhere.
using Exclusion = std::function<bool(double x, double z)>;

inline constexpr int kRingPoints = 8; // every 45 degrees
inline constexpr int kFootprintPoints = 1 + 2 * kRingPoints;

// The center, then the ring at r / 2, then the ring at r, each from +x toward +z.
[[nodiscard]] inline std::array<std::array<double, 2>, kFootprintPoints>
footprintOf(double x, double z, double reach) {
    std::array<std::array<double, 2>, kFootprintPoints> out{};
    out[0] = {x, z};
    for (int ring = 1; ring <= 2; ++ring) {
        const double r = 0.5 * ring * reach;
        for (int k = 0; k < kRingPoints; ++k) {
            const double turns = static_cast<double>(k) / kRingPoints;
            out[static_cast<std::size_t>((ring - 1) * kRingPoints + k + 1)] = {
                x + r * dmath::cosTurns(turns), z + r * dmath::sinTurns(turns)};
        }
    }
    return out;
}

// Whether no point of the footprint lies in the excluded zone.
[[nodiscard]] inline bool footprintFree(const Exclusion& excluded, double x, double z,
                                        double reach) {
    if (!excluded) {
        return true;
    }
    for (const auto& [px, pz] : footprintOf(x, z, reach)) {
        if (excluded(px, pz)) {
            return false;
        }
    }
    return true;
}

} // namespace terrain::lows
