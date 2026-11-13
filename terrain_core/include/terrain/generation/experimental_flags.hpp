#pragma once

// The experimental flags of the world (agent 51, F1): the relief of the agents of a wave is
// merged switched off, then switched on by name, for the whole world at once, from one text such
// as "seas,volcanoes" (the tools: --flags; Unreal: ATerrainWorld::Experimental). An empty text is
// the world of main, bit for bit. A name sets what its row of the table says, and nothing else.
//
// To add a flag (one line in kFlags, experimental_flags.cpp): a lower case name, what it does in
// a few words, and the function that sets the parameters of the world it switches on, then a case
// in experimental_flags_test. The name stays the same for as long as the flag lives: it is written
// in the planet files (planet_file.hpp) and in the settings of the Unreal levels.

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace terrain {

// Every parameter a flag may set: the relief, the planet level, the water (the seas with it), the
// ecology (the climate of the tiles, the biomes).
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

// The table of the flags, sorted by name.
[[nodiscard]] std::span<const ExperimentalFlag> experimentalFlags();

struct AppliedFlags {
    // The names applied, sorted, each once, joined by commas: the canonical text ("" for none),
    // the one written in the planet files.
    std::string text;
    // The names not in the table, as written, each once, in the order met: never ignored in
    // silence (the tools stop, Unreal warns in the log).
    std::vector<std::string> unknown;
};

// Applies to params the flags of text: names apart by commas or white space, their case ignored,
// empty ones skipped.
AppliedFlags applyExperimentalFlags(std::string_view text, WorldParams& params);

// The canonical text of the known names of text (AppliedFlags::text), params untouched.
[[nodiscard]] std::string canonicalFlags(std::string_view text);

// "seas, volcanoes" (the names of the table, for the usage of the tools).
[[nodiscard]] std::string knownFlagNames();

} // namespace terrain
