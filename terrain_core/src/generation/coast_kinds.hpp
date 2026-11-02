#pragma once

// The kinds of coast (agent 33, coast.hpp), internal to terrain_core: their weights at a point of
// the band of the coasts, from the geology and the relief of the hinterland and from the latitude,
// never from the biomes. Smooth in every input: no edge between two kinds.

#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain::coast_kinds {

// The noises of the kinds: the kinds of a stretch of coast alternate along it (a slow fbm), and
// its rocky and low parts at a smaller scale.
struct Noises {
    noise::FbmOctaves alternation;
    noise::FbmOctaves roughness;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// What the kinds read at a point.
struct Ground {
    float relief = 0.0f;    // intensity of the relief of the map (GeographySample::relief)
    float character = 0.5f; // worn (0) to young and sharp (1)
    float rugged = 0.5f;    // GeographySample::rugged: rugged coasts (1) or smooth ones (0.5)
    float strata = 0.5f;    // share of strata of the rock (mountain_walls::strataShare)
    double polar = 0.0;     // sin^2(pi z / world.z): 0 on the equator, 1 at the poles
};

using Weights = std::array<float, kCoastKindCount>;

// The weights of the kinds at (x, z), canonical, in [0, 1], summing to 1.
[[nodiscard]] Weights weightsAt(const Noises& noises, double x, double z, const Ground& ground);

// The heaviest kind (the first of the heaviest).
[[nodiscard]] CoastKind heaviest(const Weights& weights);

} // namespace terrain::coast_kinds
