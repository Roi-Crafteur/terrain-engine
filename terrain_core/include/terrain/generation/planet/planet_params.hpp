#pragma once

#include "terrain/generation/heightfield.hpp"

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

// Parameters of the planet level. Lengths are in meters.
struct PlanetParams {
    // Grid: square cells that divide both sides of the world (makePlanetGrid). 8,192 m gives
    // 7,328 x 4,272 cells on the default world.
    double cellSize = 8192.0;
    // Relief samples per cell and per axis, averaged (1 = the cell center only).
    int samplesPerAxis = 1;
    // Threads of the parallel stages, 0 for the hardware concurrency. The result never depends
    // on it.
    int threads = 0;

    // Below-sea components smaller than this are land depressions, not ocean: 10,000 km^2.
    double minOceanArea = 1.0e10;
};

} // namespace terrain
