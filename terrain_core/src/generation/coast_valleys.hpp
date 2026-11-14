#pragma once

// The coasts under the sea and the valleys above it (agent 31, task 11C; agent 33's coasts).
//
// The valleys of the rivers are traced on the relief without the coasts: near a coast their corridor
// may lie out over the sea, their floor tens of meters above it. The coasts fade out where the
// valleys keep less than kClearance meters away (Heightfield::leaveValleys, coastFaceClear, the
// clearance the inlets keep from the rivers): such a corridor gave back the bare relief under the
// sea, and erased the stacks and the floor of the sea there (cote_stack, stack_aiguille, when the
// network brought a river of 10 m^3/s to that coast).
//
// With HeightfieldParams::seaKeepsCoasts (the flags smooth_bands, pass_routing and sphere_latitude),
// a point whose bare relief lies under the sea keeps its coast whole when the floor of the valleys
// near it stands above the sea: a valley above the water never reaches below it. Above the water
// the coasts fade out before the valleys as before. Off, every expression is the one of before.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <cmath>

namespace terrain::coast_valleys {

// Whether a coast keeps itself whatever the valleys: enabled, its bare relief under the sea and the
// least floor of the valleys near it above the sea (a valley there: no floor without one, and the
// ponds and lakes, which have none, keep fading the coasts).
[[nodiscard]] inline bool seaKeepsCoast(bool enabled, double bare, double floor) {
    return enabled && bare < static_cast<double>(kSeaLevel) && std::isfinite(floor) &&
           floor > static_cast<double>(kSeaLevel);
}

// The weight of the faces of the coast at a point of relief height where the valleys keep near
// (Heightfield::coastFaceClear): 1 whole, 0 gone, smooth over clearance meters of clearance.
[[nodiscard]] inline float faceWeight(const Hydrology::Clearance& near, double height,
                                      double clearance, bool seaKeeps) {
    if (seaKeepsCoast(seaKeeps, height, near.floor)) {
        return 1.0f;
    }
    const double clear = near.over(height);
    if (!(clear < clearance)) {
        return 1.0f;
    }
    if (!(clear > 0.0)) {
        return 0.0f;
    }
    const double t = clear / clearance;
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

} // namespace terrain::coast_valleys
