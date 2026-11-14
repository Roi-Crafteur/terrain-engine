#pragma once

// The experimental flags of the world (agent 51): the relief of the agents of a wave is merged
// switched off, then switched on by name, for the whole world at once, from one text such as
// "seas,volcanoes" (the tools: --flags; Unreal: ATerrainWorld::Experimental). An empty text is
// the world of main, bit for bit (experimental_off_test). A name sets what its row of the table
// says, and nothing else.
//
// To add a flag: one row in the table of experimentalFlags() (experimental_flags.cpp), kept sorted
// by name: a lower case name, what it does in a few words, and the function that sets the
// parameters of the world it switches on; then a case in a test of the agent (the table of the
// flags given to applyExperimentalFlags, or experimentalFlags() itself). The name stays the same
// for as long as the flag lives: it is written in the planet files (planet_file.hpp) and in the
// settings of the Unreal levels. A flag that needs a parameter outside WorldParams adds it here.
// "none" and "reset" are no names of flags: Terrain.Flags reads them as its own words.

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace terrain {

// Every parameter a flag may set: the relief, the planet level, the water, the ecology (the
// climate of the tiles, the biomes, the materials, the plants).
struct WorldParams {
    HeightfieldParams relief;
    PlanetParams planet;
    HydrologyParams water;
    EcologyParams ecology;
};

struct ExperimentalFlag {
    const char* name; // lower case letters, digits, '-' and '_'
    const char* what; // a few words, for the usage of the tools and the log
    void (*apply)(WorldParams& params);
};

// The table of the flags, sorted by name (empty until an agent adds its row).
[[nodiscard]] std::span<const ExperimentalFlag> experimentalFlags();

struct AppliedFlags {
    // The names applied, sorted, each once, joined by commas: the canonical text ("" for none),
    // the one written in the planet files.
    std::string text;
    // The names not in the table, as written, each once, in the order met: never ignored in
    // silence (the tools stop, Unreal warns in the log).
    std::vector<std::string> unknown;
};

// Applies to params the flags of text found in table: names apart by commas, semicolons or white
// space, their case ignored, empty ones skipped. table must be sorted by name.
AppliedFlags applyExperimentalFlags(std::string_view text, WorldParams& params,
                                    std::span<const ExperimentalFlag> table = experimentalFlags());

// The canonical text of the known names of text (AppliedFlags::text), no parameter set.
[[nodiscard]] std::string canonicalFlags(std::string_view text,
                                         std::span<const ExperimentalFlag> table =
                                             experimentalFlags());

// "seas, volcanoes": the names of table, for the usage of the tools ("" for none).
[[nodiscard]] std::string knownFlagNames(std::span<const ExperimentalFlag> table =
                                             experimentalFlags());

} // namespace terrain
