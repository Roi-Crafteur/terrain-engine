#pragma once

// The seas of the world (agent 51): their floors (continental margins, the crust of the ocean by
// its age from the mid-ocean ridges, their axial valleys and highs, the fracture zones, the
// trenches of the collisions, the abyssal hills, the seamounts and the guyots), placed by the
// plate tectonics of the geography (the same plates and boundaries, sea_plates.hpp), never by
// the biome.
//
// Applied after the planet level (Heightfield::addSeas, with the water of a Hydrology): the
// planet level, its checksum and its file are not changed. Only the sea changes: a point where
// the world without the seas is not under the sea keeps every bit. The band of the coasts of
// agent 33 keeps its floor; the seas fade in from its outer edge (SeaParams::bandStart). The
// regional floor moves (HeightSample::regional, base and height together): the detail of the
// relief and whatever stands on the floor stay on it.
//
// Deterministic, thread-safe, periodic on the world; the same bits whatever the threads.

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace terrain {

class PlanetLevel;

struct SeaParams {
    // Off: no seas, not even built (the world before agent 51, bit for bit). Off by default until
    // the relief of the wave is switched on together (R1); on with the experimental flag "seas"
    // (experimental_flags.hpp).
    bool enabled = false;
    // The steps that the comparisons can switch off: the margins, the ages of the crust, the
    // ridges and trenches; the abyssal hills and the fracture zones; the seamounts.
    bool floors = true;
    bool hills = true;
    bool seamounts = true;

    // The seas fade in from bandStart to bandFull meters of coast signal at sea (the outer edge
    // of the band of the coasts, kCoastSeaBand: 20 km), over more of it where their floor moves
    // more (1 m for 20 m of signal at most).
    double bandStart = 20'000.0;
    double bandFull = 30'000.0;

    // Continental margins: the shelf, its break, the slope, its foot and the rise, each drawn
    // between its low and high by a slow noise of marginWavelength along the coasts. Passive
    // margins have wide shelves and a rise; active ones (a trench within reach: activeLow to
    // activeHigh of the active field of the plates) narrow shelves and a steeper slope going
    // down into the trench, no rise.
    double passiveShelfLow = 50'000.0;
    double passiveShelfHigh = 170'000.0;
    double activeShelfLow = 12'000.0;
    double activeShelfHigh = 35'000.0;
    double breakLow = 100.0;
    double breakHigh = 200.0;
    double passiveSlopeLow = 0.045; // 2.6 to 4.9 degrees
    double passiveSlopeHigh = 0.085;
    double activeSlopeLow = 0.07; // 4 to 6.8 degrees
    double activeSlopeHigh = 0.12;
    double footLow = 1'800.0;
    double footHigh = 3'200.0;
    double riseLow = 0.006; // 0.3 to 0.9 degrees
    double riseHigh = 0.016;
    double marginWavelength = 1'200'000.0;
    float activeLow = 0.15f;
    float activeHigh = 0.5f;
    // The small islands (land masses under islandArea, sea_shore.hpp) have narrow shelves
    // (islandShelfLow to islandShelfHigh wide, their break islandBreakLow to islandBreakHigh deep)
    // and steep slopes (islandSlopeLow to islandSlopeHigh) down to the floor, no rise; the
    // continents and the large islands the margins above.
    double islandArea = 1.0e11; // 100,000 km2
    double islandShelfLow = 2'000.0;
    double islandShelfHigh = 12'000.0;
    double islandBreakLow = 40.0;
    double islandBreakHigh = 120.0;
    double islandSlopeLow = 0.1; // 5.7 to 12.4 degrees
    double islandSlopeHigh = 0.22;
    // Marginal plateaus on terraceShare of the passive margins: a terrace terraceDepthLow to
    // terraceDepthHigh deep breaking the slope, up to terraceWidthHigh wide (narrower toward the
    // edges of the stretch of coast it follows).
    double terraceShare = 0.35;
    double terraceDepthLow = 300.0;
    double terraceDepthHigh = 1'800.0;
    double terraceWidthHigh = 300'000.0;
    // Oceanic plateaus (large igneous provinces) on about plateauShare of the floor of the ocean:
    // the crust raised to a top plateauTopLow to plateauTopHigh deep over a few hundred to two
    // thousand kilometers, where a slow noise of plateauWavelength is highest; never on an active
    // margin.
    double plateauShare = 0.05;
    double plateauWavelength = 2'500'000.0;
    double plateauTopLow = 1'000.0;
    double plateauTopHigh = 2'600.0;
    double plateauEdge = 0.05; // of the noise, from its threshold to the full plateau

    // The crust of the ocean: its age is its distance to the axis of its ridge over the half
    // spreading rate of the ridge (slowRate to fastRate meters per million years by the strength
    // of the divergence), its depth that of the age (GDH1, sea_floor::depthOfAge). The distance
    // is the exact one to the axis within ageNear, the one through the sea of the map beyond
    // ageFar. Where no ridge reaches through the sea: oldBasinDepth.
    double slowRate = 15'000.0;
    double fastRate = 80'000.0;
    double ageNear = 250'000.0;
    double ageFar = 450'000.0;
    double oldBasinDepth = 5'000.0;
    // The axis: the rift valley of the slow ridges, the axial high of the fast ones.
    double valleyDepth = 1'400.0;
    double valleyFloor = 5'000.0;
    double valleyWidth = 16'000.0;
    double highHeight = 350.0;
    double highWidth = 7'000.0;

    // The trenches of the collisions at sea, along the axis the geography gives them (1.35 half
    // widths of the belt beyond its axis on the subducting side): a V trenchHalfWidth wide each
    // side, trenchDrop under the floor at the strongest (the trench field of the tectonics at its
    // peak), an outer rise outerRise high outerRiseAt half widths out on the subducting plate
    // (outerRiseWidth of them each side); faded in from trenchNear to trenchFull meters from the
    // shore.
    double trenchDrop = 4'500.0;
    double trenchHalfWidth = 45'000.0;
    double outerRise = 400.0;
    double outerRiseAt = 2.5;
    double outerRiseWidth = 1.5;
    double trenchNear = 20'000.0;
    double trenchFull = 60'000.0;

    // Fracture zones across the ridges, along the spreading: about fractureShare of the cells of
    // fractureSpacing along each axis hold one; a trough fractureWidth wide and fractureDepthLow
    // to fractureDepthHigh deep, and the axis offset by up to transformOffset between two of
    // them (the transform faults), the offset blended over transformBlend across each.
    double fractureSpacing = 300'000.0;
    double fractureShare = 0.8;
    double fractureWidth = 6'000.0;
    double fractureDepthLow = 200.0;
    double fractureDepthHigh = 900.0;
    double transformOffset = 60'000.0;
    double transformBlend = 20'000.0;

    // Abyssal hills, elongated along the axis of their ridge (hillsAlong by hillsAcross): an rms
    // of hillsSlow on the crust of the slowest ridges to hillsFast on that of the fastest, varied
    // by hillsVariation, buried under the sediments of the passive margins (all of it within
    // sedimentNear of the shore, nothing beyond sedimentFar) and of the old crust (sedimentOld of
    // it from sedimentAge million years on).
    double hillsAcross = 4'000.0;
    double hillsAlong = 15'000.0;
    double hillsSlow = 160.0;
    double hillsFast = 60.0;
    double hillsVariation = 0.3;
    double sedimentNear = 150'000.0;
    double sedimentFar = 900'000.0;
    double sedimentAge = 120.0;
    double sedimentOld = 0.6;

    // Isolated seamounts: in provinces of a slow noise of seamountClusterWavelength, dense in some
    // and sparse in others, about 0.64 seamountShare of the cells of seamountCell meters hold one
    // (from 0.15 to 1.85 times seamountShare), seamountLow
    // to seamountHigh meters high (more low ones), their flanks falling flankLow to flankHigh per
    // meter, their summits at least summitDepth under the sea (the islands are for the chains of
    // the hotspots). Beyond guyotLatitude of the way to the poles, guyotShare of them are flat
    // topped, their top guyotTopLow to guyotTopHigh under the sea.
    double seamountCell = 65'536.0; // divides every side of a world
    double seamountShare = 0.6;     // in an average province (dense or sparse: a slow noise)
    double seamountClusterWavelength = 1'500'000.0;
    double seamountLow = 600.0;
    double seamountHigh = 3'000.0;
    double flankLow = 0.15;
    double flankHigh = 0.32;
    double summitDepth = 300.0;
    double guyotLatitude = 0.2;
    double guyotShare = 0.3;
    double guyotTopLow = 1'000.0;
    double guyotTopHigh = 1'800.0;
};

// What the seas make of a point of the sea (Seas::at), for the probe and the tests.
struct SeaPoint {
    bool zone = false;   // under the sea of the world without the seas, out of the band
    double fade = 0.0;   // [0, 1], from the band of the coasts
    double before = 0.0; // the regional floor of the geography
    double shore = 0.0;  // meters to the nearest shore (sea_shore.hpp, the coast signal near it)
    double continent = 0.0; // meters to the shore of the nearest large land
    double floor = 0.0;  // the regional floor of the seas (before the fade)
    double margin = 0.0; // the continental margin there
    double plateau = 0.0; // [0, 1], an oceanic plateau
    double crust = 0.0;  // the crust of the ocean there (age, axis, fracture zones)
    int ridge = -1;      // the nearest ridge through the sea (sea_plates::Ridge), -1 for none
    double ridgeDistance = -1.0; // meters to its axis, -1 for none
    double age = -1.0;           // million years, -1 for none
    double rate = 0.0;           // [0, 1], the share of the fastest spreading
    double active = 0.0;         // [0, 1], active margin
    double trench = 0.0;         // meters the trench adds under the floor
    double outerRise = 0.0;      // meters the outer rise of a trench raises it
    double hills = 0.0;          // meters
    double fracture = 0.0;       // meters the fracture zones dig
    double seamount = 0.0;       // meters the seamounts raise
};

class Seas {
public:
    // The seas of relief (with its geography, which must outlive them), whose Heightfield has the
    // seed seed, built on up to threads threads (0: the hardware concurrency). The planet level is read for the climate of
    // the seas to come (nullptr allowed). Throws std::invalid_argument without a geography.
    Seas(std::uint64_t seed, const Heightfield& relief, const PlanetLevel* planet,
         const SeaParams& params, int threads = 0);
    ~Seas();
    Seas(const Seas&) = delete;
    Seas& operator=(const Seas&) = delete;

    // The breakdown at the canonical point (x, z) of the sample s of the world without the seas,
    // for squares of footprint meters (0: a point).
    [[nodiscard]] SeaPoint at(double x, double z, const HeightSample& s, double footprint) const;
    // s with the seas: unchanged out of the zone (SeaPoint::zone false).
    void apply(double x, double z, HeightSample& s, double footprint) const;
    // The breakdown at the canonical point (x, z) from the geography alone (its regional floor,
    // without the shapes of the coasts nor the water): what the maps draw at every zoom; zone
    // false on land and in the band of the coasts. floor is then the floor of the world.
    [[nodiscard]] SeaPoint mapAt(double x, double z, double footprint) const;

    [[nodiscard]] const SeaParams& params() const;
    [[nodiscard]] std::size_t memoryBytes() const;
    // 64-bit FNV-1a of the fields the seas built (the same whatever the threads).
    [[nodiscard]] std::uint64_t checksum() const;

    struct Impl;

private:
    std::unique_ptr<const Impl> impl_;
};

} // namespace terrain
