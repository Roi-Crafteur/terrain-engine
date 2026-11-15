#pragma once

// The fingerprint of a relief (agent 55, task T4 of the audit of agent 53): what the planet level
// is built from, written in the planet file (section PROV, storage/planet_sections.cpp), so that a
// file of another relief is refused as stale even when kReliefGeneratorVersion did not move.

#include "terrain/generation/heightfield.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// The parameters of a relief as bytes: every field of HeightfieldParams, of its GeographyParams
// and of their VoronoiParams, its value written in a fixed order (little endian; a bool as one
// byte), never the memory of the structures (their padding is not deterministic). A field added to
// one of them must be added here: their sizes are pinned in relief_fingerprint.cpp to force it.
[[nodiscard]] std::vector<std::uint8_t> reliefParamsBytes(const HeightfieldParams& params);

// Side of the grid of the points of the fingerprint: kFingerprintSide x kFingerprintSide points at
// the centers of the cells of the world.
inline constexpr int kFingerprintSide = 16;

// FNV-1a 64 of reliefParamsBytes(relief.params()), of the world (its sides, as doubles) and of the
// bits of relief.withoutCoasts().heightAt at the 256 points of the grid: the relief the planet
// level is built over. The same on every platform and thread.
[[nodiscard]] std::uint64_t reliefFingerprint(const Heightfield& relief);

} // namespace terrain
