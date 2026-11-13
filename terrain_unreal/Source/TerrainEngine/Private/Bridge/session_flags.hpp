#pragma once

// The experimental flags of an Unreal level (agent 51, terrain/generation/experimental_flags.hpp):
// the text of ATerrainWorld::Experimental.ExperimentalFlags, the parameters of the world they set
// for its session (the relief, the planet level, the water, the ecology), and the lines that tell
// them in the log at the start of the session, an unknown name never ignored in silence.

#include "terrain/generation/experimental_flags.hpp"

#include <span>
#include <string>
#include <vector>

namespace terrain::unreal {

struct SessionFlags {
    WorldParams params;   // the parameters of the world the flags set
    AppliedFlags applied; // their canonical text, the names unknown
};

// The flags of text, by the rows of table (the table of the world but in the tests).
[[nodiscard]] SessionFlags sessionFlags(const std::string& text,
                                        std::span<const ExperimentalFlag> table =
                                            experimentalFlags());

// A line of the log: a warning, or a plain line.
struct FlagLine {
    bool warning = false;
    std::string text;

    friend bool operator==(const FlagLine&, const FlagLine&) = default;
};

// "Terrain World: experimental flags: none", or ...: "seas,volcanoes" (the canonical text, quoted),
// followed by " (from <from>)" when from is not empty (from Terrain.Flags); then a warning per
// unknown name: "Terrain World: unknown experimental flag 'x' (known: a, b)", "(no flag known
// yet)" when table is empty.
[[nodiscard]] std::vector<FlagLine> flagLines(const AppliedFlags& applied,
                                              std::span<const ExperimentalFlag> table =
                                                  experimentalFlags(),
                                              const std::string& from = {});

} // namespace terrain::unreal
