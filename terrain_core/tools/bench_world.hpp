#pragma once

// The tiles, the parts of a LOD 0 tile and the height grids of terrain_bench_noise, on the
// default world and on the planet (bench_world.cpp).

#include "bench_support.hpp"

#include "terrain/generation/heightfield.hpp"

namespace bench {

void worldCases(const Config& c, const terrain::Heightfield& field,
                const terrain::Heightfield& planet);

} // namespace bench
