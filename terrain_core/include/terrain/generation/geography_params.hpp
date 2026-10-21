#pragma once

#include "terrain/generation/voronoi.hpp"
#include "terrain/world_size.hpp"

namespace terrain {

// The planetary geography of phase 11R (generator 3): continents, coasts, ocean floors and a
// drained regional elevation, computed once on a periodic map of the world when the Heightfield
// is built, then read at every point (Geography). Lengths in meters, altitudes in meters.
struct GeographyParams {
    // Off: the relief of generator 2 (local continentalness, macro and plate layers).
    bool enabled = false;

    // Nodes of the map, about this far apart (a whole number along each side of the world).
    double cellSize = 65'536.0;

    // Continental crust: continental cores (ellipses of various sizes and elongations spread
    // over the latitudes within coreLatitudes of the way to the poles), their coordinates moved
    // by a warp fbm of crustWarpAmplitude, roughened by crustRoughness times an fbm of
    // crustOctaves from crustWavelength (unrotated lattices), minus polarBias times a ramp from
    // polarStart of the way to the poles, thresholded so that landShare of the map is land.
    double crustWavelength = 4'000'000.0;
    int crustOctaves = 5;
    double crustRoughness = 1.3;
    double crustWarpWavelength = 6'000'000.0;
    double crustWarpAmplitude = 2'000'000.0;
    float landShare = 0.37f;
    float polarBias = 0.6f;
    double polarStart = 0.7;
    double coreLatitudes = 0.8;
    // Among crustCandidates streams (each with its minContinents + 1 to maxContinents - 1
    // cores), the first whose land masses of at least continentArea number minContinents to
    // maxContinents, the largest holding at most largestShare of the land; else the closest.
    // Smaller water bodies than inlandSeaArea inside the land are filled.
    int crustCandidates = 8;
    int minContinents = 5;
    int maxContinents = 10;
    double continentArea = 1.0e13; // 10 million km2
    float largestShare = 0.35f;
    double inlandSeaArea = 3.0e10; // 30,000 km2

    // Coasts at every scale, three fbm stages added to the distance to the coast of the map,
    // each within a band of the coast of the stages before it (and skipped beyond): coarse
    // gulfs and peninsulas (coastWavelength, coastAmplitude, within the band of half width
    // coastBand), capes and coastal islands (detailCoast*), coves, points and rocks at the scale
    // of a walk (fineCoast*). Each band is at least 2.5 times its amplitude, so that a coast
    // never folds onto itself.
    double coastWavelength = 600'000.0;
    double coastAmplitude = 250'000.0;
    double coastBand = 700'000.0;
    double detailCoastWavelength = 32'000.0;
    double detailCoastAmplitude = 22'000.0;
    double detailCoastBand = 60'000.0;
    double fineCoastWavelength = 6'000.0;
    double fineCoastAmplitude = 6'000.0;
    double fineCoastBand = 16'000.0;
    // The detail and fine coasts are rugged in some regions (rias, fjords, skerries), smooth in
    // others (beaches, deltas): their amplitude is scaled by smoothRugged up to 1, by an fbm of
    // ruggedWavelength.
    double ruggedWavelength = 2'500'000.0;
    float smoothRugged = 0.25f;

    // Continental shelf: from the coast down to shelfDepth over shelfWidth, then the slope
    // (shelfSlope) to the floor of the map: continental rise and abyssal plain at about
    // abyssDepth.
    float shelfDepth = -130.0f;
    double shelfWidth = 90'000.0;
    double shelfSlope = 0.035;
    float abyssDepth = -4600.0f;

    // Land: rises from the coast over coastRamp to the regional elevation, which climbs at
    // least drainSlope along the way water takes to the sea (a priority flood of the map: no
    // closed depression at its scale), on a continental tilt of inlandRise per meter from the
    // coast.
    double coastRamp = 30'000.0;
    double drainSlope = 1.0e-4;
    double inlandRise = 1.5e-4;

    // Tectonic plates of the geography (their layout, motions and boundaries).
    VoronoiParams plates;

    friend bool operator==(const GeographyParams&, const GeographyParams&) = default;
};

} // namespace terrain
