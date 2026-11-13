#pragma once

// --flags of the tools (agent 51, F1): the experimental flags of the world, its relief switched
// on by name (terrain/generation/experimental_flags.hpp). An unknown name stops the tool.

#include "terrain/generation/experimental_flags.hpp"

#include <cstdio>
#include <string>

namespace tool_flags {

// params set by the flags of text, canonical their canonical text (the one of the planet files);
// false, each unknown name told on stderr with the known ones, if any is unknown.
inline bool apply(const std::string& text, terrain::WorldParams& params, std::string& canonical) {
    const terrain::AppliedFlags applied = terrain::applyExperimentalFlags(text, params);
    for (const std::string& name : applied.unknown) {
        std::fprintf(stderr, "Unknown experimental flag '%s' (known: %s)\n", name.c_str(),
                     terrain::knownFlagNames().c_str());
    }
    canonical = applied.text;
    return applied.unknown.empty();
}

} // namespace tool_flags
