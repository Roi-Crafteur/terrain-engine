#pragma once

// The experimental flags of task 11C (agent 31) in the tests: the parameters of a planet level with
// flags on (experimental_flags.hpp), as the tools and Unreal set them.

#include "terrain/generation/experimental_flags.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace network_flags {

// The flags of step 1, all on (what the orchestrator switches on in R2).
inline constexpr std::string_view kStepOne = "pass_routing,planet_network,smooth_bands";

// planet with the flags of text on; throws on an unknown name.
inline terrain::PlanetParams flagged(const terrain::PlanetParams& planet, std::string_view text) {
    terrain::WorldParams world;
    world.planet = planet;
    const terrain::AppliedFlags applied = terrain::applyExperimentalFlags(text, world);
    if (!applied.unknown.empty()) {
        throw std::invalid_argument("unknown experimental flag " + applied.unknown.front());
    }
    return world.planet;
}

} // namespace network_flags
