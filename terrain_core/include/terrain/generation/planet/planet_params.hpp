#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_breach.hpp"

namespace terrain {

// The "planetary" relief: tectonic plates 0.5 with the macro-continental layer 0.25 (the maps
// of terrain_export_voronoi), real continents and oceans at the scale of the planet. The
// default HeightfieldParams draw land and sea from a 60 km noise only: an archipelago at that
// scale. The planet level takes either.
[[nodiscard]] inline HeightfieldParams planetaryHeightfieldParams() {
    HeightfieldParams params;
    params.plates.amplitude = 0.5f;
    params.macroContinent = 0.25f;
    return params;
}

// Continental climate of the planet level. Temperatures are in degrees Celsius, precipitation
// and evaporation in mm per year, lengths in meters.
struct PlanetClimateParams {
    // Mean annual temperature: the latitude curve of ClimateField (equatorTemperature at the
    // equator, poleTemperature at the poles, s^1.5 with s = sin^2(pi z / world.z)), minus
    // lapseRate per meter above sea level.
    float equatorTemperature = 27.0f;
    float poleTemperature = -23.0f;
    float lapseRate = 0.0065f;
    // Below this mean temperature a cell is frozen: its water moves on as ice, no liquid river
    // flows there and its lakes do not evaporate. The flag sphere_latitude makes it the snow of
    // the biomes (BiomeParams::snowTemperature, -6): one threshold of the frost.
    float iceTemperature = -10.0f;
    // The latitude of a sphere (climate_latitude.hpp; flag sphere_latitude): sin(lat) = z / zmax.
    bool sphereLatitude = false;
    // The climate of a land cell reads the mean altitude of its points (PlanetCells::climateRise),
    // its water the floor (flag sphere_latitude). Off, the floor for both, as main.
    bool meanAltitude = false;

    // Prevailing winds, one direction per latitude band: the trades (0 to 30 degrees) blow to
    // the west, the westerlies (30 to 60) to the east, the polar easterlies (60 to 90) to the
    // west. These are their meridional components for a zonal component of 1, poleward when
    // positive.
    double tradeMeridional = -0.4;
    double westerlyMeridional = 0.3;
    double polarMeridional = -0.3;
    // With smoothBands (flag smooth_bands, task 11C), the bands join smoothly (planet_bands.hpp):
    // over bandJoin degrees of latitude on each side of the lines between them (60 and 30
    // degrees, the equator), lines moved north and south by up to bandShift degrees by a slow
    // wave of the longitude, about bandShiftWavelength long. Off, they meet at once, as before.
    bool smoothBands = false;
    double bandJoin = 5.0;
    double bandShift = 3.0;
    double bandShiftWavelength = 3.0e6;

    // Moisture carried by the wind, in mm/yr x m: over a warm ocean the air holds enough water
    // to rain coastPrecipitation mm/yr at 15 degrees, times exp(moistureGrowth (T - 15)).
    double coastPrecipitation = 1100.0;
    double moistureGrowth = 0.07;
    // E-folding lengths: of the rain over flat ground, of the evaporation that brings the air
    // over the ocean back to saturation, and of the rise of the air that rains the moisture out.
    double rainLength = 3.0e6;
    double evaporationLength = 4.0e5;
    double upliftHeight = 1500.0;
    // The air keeps the altitude it was lifted to and comes down slowly: its height above the
    // ground decays over this length, so that a valley or an arm of the sea between two
    // heights does not lift it twice.
    double airDescentLength = 3.0e5;
    // The air flows over the relief smoothed over this radius (a box blur of the ground, the
    // sea at 0, twice along each axis), not over every ridge of the noise: whole ranges lift
    // it, single peaks do not.
    double orographyRadius = 5.0e4;
    // The air holds at most capacityFactor times the saturation moisture of its temperature
    // (cold air at altitude holds less: orographic rain, then the rain shadow behind).
    double capacityFactor = 1.5;
    // Share of the evapotranspiration of the land that goes back into the air: all of it
    // physically, so that over land the air only loses what runs off (moisture far inland, as
    // over the Amazon); less to dry the interiors.
    double recycling = 1.0;
    // Transient eddies (storms) bring the moisture of the nearby seas from every direction: the
    // saturation moisture of the sea cells blurred over eddyRadius (two box passes per axis)
    // adds eddyShare of its rain over land, fading with the height of the orographic surface
    // over eddyHeight. Deep continental interiors stay dry.
    double eddyRadius = 6.0e5;
    double eddyShare = 0.5;
    double eddyHeight = 2000.0;
    // Latitude factor of the rain rate: 1 + itcz g(0, 8) - subtropicalDip g(25, 9) +
    // polarFront g(52, 10), with g(center, width) = exp(-((latitude - center) / width)^2) in
    // degrees, at least minRainFactor: wet equator, dry subtropics (descending air), wet polar
    // front.
    double itcz = 1.2;
    double subtropicalDip = 0.8;
    double polarFront = 1.0;
    double minRainFactor = 0.15;
    // Passes of a [1 2 1] / 4 blur of the precipitation along each axis.
    int blurPasses = 2;

    // Potential evapotranspiration: petRate mm/yr per degree above petZero.
    double petRate = 65.0;
    double petZero = -2.0;
    // Land runoff P - P PET / sqrt(P^2 + PET^2) (Turc-Pike), scaled by smoothstep(aridityLow,
    // aridityHigh, P / PET): no runoff in a desert, so no river springs there.
    double aridityLow = 0.15;
    double aridityHigh = 0.4;
};

// The river graph of the planet level.
struct PlanetRiverParams {
    // A dry, unfrozen land cell is a river where at least this many m^3/s leave it.
    double minDischarge = 10.0;
    // Bankfull width and depth of a river in meters from its discharge Q (hydraulic geometry,
    // Leopold and Maddock): widthCoefficient Q^0.5 and depthCoefficient Q^depthExponent.
    double widthCoefficient = 7.0;
    double depthCoefficient = 0.3;
    double depthExponent = 0.4;
    // The rivers go on under the ice (flag sphere_latitude): a frozen cell is a river as any
    // other. Off, as main: no river on frozen ground.
    bool underIce = false;
};

// Places of the river graph where 11B carves remarkable forms. Discharges in m^3/s, lengths in
// meters, slopes in meters per meter.
struct PlanetFeatureParams {
    // A sea mouth of at least deltaMinDischarge is a delta if the river falls by at most
    // deltaMaxSlope over its last mouthReach steps and the sea next to the mouth is at most
    // shelfDepth deep; otherwise, from estuaryMinDischarge, an estuary.
    double deltaMinDischarge = 1000.0;
    double deltaMaxSlope = 3.0e-4;
    double shelfDepth = 200.0;
    int mouthReach = 3;
    double estuaryMinDischarge = 1000.0;
    // A waterfall where a step of a river of at least fallMinDischarge drops by fallMinDrop or
    // more, at a slope of at least fallMinSlope and fallBreak times the steeper of the steps
    // just above and just below: falls only at the breaks of slope (never on the first step of
    // a plain source, whose slope above is unknown; a lake outlet has its flat lake above).
    double fallMinDischarge = 100.0;
    double fallMinDrop = 50.0;
    double fallMinSlope = 0.005;
    double fallBreak = 3.0;
    // A gorge where the ground rises at least gorgeMinDepth above a river of at least
    // gorgeMinDischarge on both sides, within 2 cells across the flow.
    double gorgeMinDischarge = 100.0;
    double gorgeMinDepth = 800.0;
};

// The links of the cells under the mountains through the passes (planet_links.hpp): a link is
// open where the way between the floors climbs at most tolerance meters above the floor of the
// cell (read on the samples of the floors). Off (flag pass_routing, task 11C): the steepest
// neighbor below, as before, and the exits of the cells are not even sampled.
struct PlanetLinkParams {
    bool enabled = false;
    double tolerance = 40.0;
};

// Parameters of the planet level. Lengths are in meters.
struct PlanetParams {
    // Grid: square cells that divide both sides of the world (makePlanetGrid). 8,192 m gives
    // 7,328 x 4,272 cells on the default world.
    double cellSize = 8192.0;
    // Relief samples per cell and per axis, averaged (1 = the cell center only).
    int samplesPerAxis = 1;
    // With floorSamples above 1, the cells of the land (and of the sea near it) take their floor
    // instead, the lowest of floorSamples^2 points (planet::sampleCells), and their rivers pass
    // there: the water of a cell gathers at its low point, not at its center on a slope or on a
    // hill (most false lakes of one cell).
    int floorSamples = 3;
    // Threads of the parallel stages, 0 for the hardware concurrency. The result never depends
    // on it.
    int threads = 0;

    // Below-sea components smaller than this are land depressions, not ocean: 10,000 km^2.
    double minOceanArea = 1.0e10;

    // The rivers cut through the sills of the shallow depressions (planet::breachDepressions),
    // or none of them without breachSills.
    bool breachSills = true;
    PlanetBreachParams breach;

    PlanetClimateParams climate;
    PlanetLinkParams links;
    PlanetRiverParams rivers;
    PlanetFeatureParams features;
    // The level keeps the discharge of every land cell (PlanetCells::landDischarge), what the
    // network of the planet level and the fine network read (planet_network.hpp; flag
    // planet_network, task 11C). Off, the level, its checksum and its file are as before.
    bool landDischarge = false;
};

} // namespace terrain
