#pragma once

#include "terrain/generation/voronoi.hpp"
#include "terrain/world_size.hpp"

namespace terrain {

// The planetary geography of phase 11R (generator 3): continents, coasts, ocean floors and a
// drained regional elevation, computed once on a periodic map of the world when the Heightfield
// is built, then read at every point (Geography). Lengths in meters, altitudes in meters.
struct GeographyParams {
    // On by default (generator 3). Off: the relief of generator 2 (local continentalness, macro
    // and plate layers).
    bool enabled = true;

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
    int crustCandidates = 16;
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
    double detailCoastAmplitude = 50'000.0;
    double detailCoastBand = 130'000.0;
    double fineCoastWavelength = 10'000.0;
    double fineCoastAmplitude = 22'000.0;
    double fineCoastBand = 56'000.0;
    // The detail and fine coasts are rugged in some regions (rias, fjords, skerries), smooth in
    // others (beaches, deltas): their amplitude is scaled by smoothRugged up to 1, by an fbm of
    // ruggedWavelength.
    double ruggedWavelength = 2'500'000.0;
    float smoothRugged = 0.5f;

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
    double drainSlope = 6.0e-5;
    double inlandRise = 0.8e-4;
    // The flood reaches the nodes in the order of their elevations jittered by up to drainJitter:
    // where the land is gentle its drainage branches like a tree instead of running down in
    // parallel lines.
    float drainJitter = 60.0f;

    // Provinces of the land between the belts, their sites on a jittered grid about provinceSize
    // apart, the nearest to each point after a warp of provinceWarp: lowlands, sedimentary
    // basins, high plains, shields with hills, old worn massifs, plateaus and intraplate high
    // relief, with more lowlands and basins in the forelands within forelandDistance of a belt.
    // Their uplift, the intensity and the character of their relief blend over provinceBlend.
    double provinceSize = 2'000'000.0;
    double provinceWarp = 900'000.0;
    double forelandDistance = 600'000.0;
    double provinceBlend = 600'000.0;

    // Tectonic plates of the geography: their layout, motions and boundaries (the amplitudes,
    // widths and kinds of VoronoiParams are not used: the geography has its own below). About
    // as many plates for the area of the world as on the Earth.
    VoronoiParams plates{.minPlates = 48, .maxPlates = 72};

    // Mountain belts along the collisions of the plates: half width beltWidth at the axis (0.25
    // to 2 times it from one boundary to another, and +-30 % along a boundary), centered
    // beltOffset half widths toward the overriding plate; on land they lift the regional
    // elevation by beltUplift at their axis (+-35 %) and carry the mountains. chainlessShare of
    // the collisions raise foothills only, without a chain.
    double beltWidth = 200'000.0;
    double beltOffset = 0.35;
    float beltUplift = 2000.0f;
    float chainlessShare = 0.25f;
    // High plateaus behind plateauShare of the collisions longer than plateauLength:
    // plateauUplift (+-10 %) over plateauWidth (0.6 to 1.4 times it) on the overriding side.
    float plateauShare = 0.3f;
    double plateauLength = 2'000'000.0;
    float plateauUplift = 3800.0f;
    double plateauWidth = 700'000.0;
    // The intensity of the relief (phase 11R bis), a field of the map from reliefFloor on the
    // land to 1, without any edge: the belts with a tail decaying as exp(-d / beltReach) into
    // foothills then hills, varied along the chains by +-reliefVariation (high and low parts,
    // passes, intramontane basins); the provinces; the high plateaus (plateauRelief); isolated
    // massifs in the plains up to massifRelief. Spurs and secondary ranges leave the chains up
    // to spurRelief more, along the lines of a ridged noise of spurWavelength.
    double beltReach = 200'000.0;
    float reliefFloor = 0.03f;
    float reliefVariation = 0.35f;
    float plateauRelief = 0.42f;
    float massifRelief = 0.5f;
    float spurRelief = 0.35f;
    double spurWavelength = 180'000.0;
    // Steps of the relief that the comparisons can switch off: the ravines of the mountain slopes
    // and the lively plains (vallons and buttes everywhere, terraces, more relief at the scale
    // of a walk); the valleys in U, the dominant summits and the rock walls of the mountains; the
    // forms of the plains (kinds of plains, swells, hills, micro relief, a network of vallons,
    // isolated hills), only where the mask of the mountains is 0.
    bool ravines = true;
    bool livelyPlains = true;
    bool valleys = true;
    bool summits = true;
    bool walls = true;
    bool plainForms = true;
    // The depressions below the sea level of phase 11B, deep in the plains of the continents, only
    // where the mask of the mountains is 0 (depressions.hpp).
    bool depressions = true;
    // The relief of the coasts (agent 33, coast.hpp): cliffs, mountains plunging into the sea,
    // fjords, rias, calanques, coves, in the band of the coast signal from kCoastSeaBand at sea to
    // coastRamp inland. Off: the coastal plains of phase 11R. The planet level and the rivers are
    // built without it (Heightfield::withoutCoasts).
    bool coasts = true;

    // The ridged mountains of the Heightfield (its warp and octaves) rise up to mountainHeight
    // above the regional elevation under their mask, with their own wavelength, sharpness and
    // gain: sharp crests, valleys in V between them, steep walls without a fractal roughness at
    // every scale.
    float mountainHeight = 4300.0f;
    double mountainWavelength = 14'000.0;
    double mountainSharpness = 4.0;
    double mountainGain = 0.42;
    // The valleys between the crests keep at least this share of the finer octaves of the
    // ridged noise (FbmOctaves::ridged2DSlope): secondary crests and hollows, not smooth floors.
    double mountainValleyDetail = 0.2;
    // Under the mask of the massifs, a rock of up to rockRelief meters breaks the walls and the
    // floors at the scale of a walk: the sharp crests of a ridged noise of rockOctaves from
    // rockWavelength, not warped (no curls), its octaves turned and offset (no line, no grid).
    float rockRelief = 35.0f;
    double rockWavelength = 400.0;
    int rockOctaves = 4;
    // The detail of the Heightfield (its wavelength) over the land of the geography: up to
    // detailAmplitude meters.
    float detailAmplitude = 6.0f;
    // Island arcs where a belt crosses the ocean: the coast signal rises to about arcLift on
    // their axis, broken into islands by an fbm of arcWavelength.
    double arcLift = 45'000.0;
    double arcWavelength = 90'000.0;
    // Trenches on the subducting side of the collisions at sea, mid-ocean ridges along the
    // divergences between oceans.
    float trenchDepth = 3200.0f;
    double trenchWidth = 110'000.0;
    float ridgeLift = 2000.0f;
    double ridgeWidth = 450'000.0;
    // Continental rifts along riftShare of the strongest divergences: a trough of half width
    // riftWidth whose flat floor lies riftDepthLow to riftDepthHigh (drawn for each rift) below
    // its rim, its walls in steps (tilted fault blocks), between shoulders at least riftShoulder
    // high (more where the land is low), on land.
    float riftShare = 0.5f;
    float riftDepthLow = 1500.0f;
    float riftDepthHigh = 2500.0f;
    double riftWidth = 130'000.0;
    float riftShoulder = 1000.0f;
    // The lowest the regional elevation of the land may go (the floors of the rifts): above the
    // sea for now, since the surface puts the sea wherever the ground is below 0 m; the code of
    // the rifts allows less once the sea follows the geography instead (phase 11B).
    float landMin = 1.0f;

    friend bool operator==(const GeographyParams&, const GeographyParams&) = default;
};

} // namespace terrain
