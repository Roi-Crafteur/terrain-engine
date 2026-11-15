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
// near it stands above the sea: a valley above the water never reaches below it. And a cove may be
// cut under a valley perched above its ground (the clearance the inlets keep from the rivers), so
// that the reefs and stacks between the coves stay: the cove still fades out inside the corridor
// of the valley (leaveValleys, above the water as before), where the bed and the water of the river
// keep the relief of the valley; the river comes down to the sea at its mouth, its falls only where
// its own course (traced on the relief without the coasts) breaks its slope. Off, every expression
// is the one of before.

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

// Whether a cove may be cut at a point of bare relief ground below the valleys near it: enabled,
// their least floor above that ground (a valley perched above the cove), or the point under the
// sea below a valley above it.
[[nodiscard]] inline bool covePasses(bool enabled, double ground, double floor) {
    return enabled && std::isfinite(floor) && ground < floor;
}

// How far the valleys keep from a coast at a point of bare relief ground (meters; the coasts fade
// out within the clearance of heightfield_coast.cpp): the least of how far across and how far
// above their floor (Hydrology::Clearance::over), as main; with seaKeeps, across only where they
// stand perched above the ground (their floor higher): a valley above a cove reaches it only by
// its corridor, never from above.
[[nodiscard]] inline double coastClearance(const Hydrology::Clearance& near, double ground,
                                           bool seaKeeps) {
    if (seaKeeps && std::isfinite(near.floor) && ground < near.floor) {
        return near.away;
    }
    return near.over(ground);
}

// What the coast becomes at a point of bare relief bare where the valleys keep near
// (Heightfield::leaveValleys): the coast whole, the bare relief, or a blend of weight weight (the
// coast's share).
enum class ValleyFade : unsigned char { Coast, Bare, Blend };
struct ValleyBlend {
    ValleyFade fade = ValleyFade::Coast;
    float weight = 1.0f;
};
[[nodiscard]] inline ValleyBlend valleyBlend(const Hydrology::Clearance& near, double bare,
                                             double clearance, bool seaKeeps) {
    if (seaKeepsCoast(seaKeeps, bare, near.floor)) {
        return {};
    }
    const double clear = coastClearance(near, bare, seaKeeps);
    if (!(clear < clearance)) {
        return {};
    }
    if (!(clear > 0.0)) {
        return {ValleyFade::Bare, 0.0f};
    }
    const double t = clear / clearance;
    return {ValleyFade::Blend, static_cast<float>(t * t * (3.0 - 2.0 * t))};
}

// The weight of the faces of the coast at a point of relief height where the valleys keep near
// (Heightfield::coastFaceClear): 1 whole, 0 gone, smooth over clearance meters of clearance.
[[nodiscard]] inline float faceWeight(const Hydrology::Clearance& near, double height,
                                      double clearance, bool seaKeeps) {
    if (seaKeepsCoast(seaKeeps, height, near.floor)) {
        return 1.0f;
    }
    const double clear = coastClearance(near, height, seaKeeps);
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
