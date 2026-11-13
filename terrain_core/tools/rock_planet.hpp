#pragma once

// The planet level of terrain_export_rock (export_rock.cpp): read from its file, or built and
// written there; with experimental flags (tool_flags.hpp), built in memory and never written (the
// planet files do not hold their flags yet).

#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/storage/planet_file.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace rock_tool {

// The planet level of the default seed and world of params on threads threads (0: all), canonical
// the canonical text of its flags; built tells whether it was built.
inline std::shared_ptr<const terrain::PlanetLevel>
openPlanet(const terrain::WorldParams& world, const std::string& canonical,
           const std::filesystem::path& file, int threads, bool& built) {
    const std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::PlanetParams params = world.planet;
    params.threads = threads;
    if (canonical.empty()) {
        return std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
            file, seed, world.relief, params, terrain::WorldSize{}, &built));
    }
    built = true;
    const terrain::Heightfield field(seed, world.relief);
    return std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildLevelParts(field, params));
}

} // namespace rock_tool
