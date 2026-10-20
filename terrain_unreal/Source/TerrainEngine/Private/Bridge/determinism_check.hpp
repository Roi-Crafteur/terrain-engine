#pragma once

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace terrain::unreal {

// A height of the default world (kDefaultWorldSeed, HeightfieldParams{}): heightAt(x, z) bit for
// bit, as the core built by CMake computes it (checked by unreal_determinism_check_test).
struct HeightProbe {
    double x = 0.0;
    double z = 0.0;
    std::uint32_t bits = 0; // std::bit_cast<std::uint32_t>(heightAt(x, z))
};

// The probes: the origin, a slope, a mountain, the sea floor, and far points up to the size of
// the planet, with negative and fractional coordinates.
// TODO(phase 9): the values of the terrain change with the wrapped world and the math functions
// of the core: update this table in the same commit as the core (the ctest test fails until
// then).
[[nodiscard]] std::span<const HeightProbe> heightProbes();

struct DeterminismReport {
    bool same = true;
    // With same false: the first probe that differs and the bits of the height computed instead.
    HeightProbe probe;
    std::uint32_t actualBits = 0;
};

// Compares the heights of the field with the probes. The field must be the default world for
// the probes to hold.
[[nodiscard]] DeterminismReport checkHeights(const Heightfield& field);

// The check of this build: the heights of the default world against the probes, whatever the
// seed of the level. An Unreal build whose floating point differs from the core's generates
// another world; it must not stream.
[[nodiscard]] DeterminismReport checkDefaultWorld();

// For the log: "the heights match" or "this build does not generate the same world: ...".
[[nodiscard]] std::string describe(const DeterminismReport& report);

} // namespace terrain::unreal
