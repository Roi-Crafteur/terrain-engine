#pragma once

// The planet level of terrain_export_rock (export_rock.cpp): read from its file, or built and
// written there. With experimental flags (tool_flags.hpp) the file holds them (planet file of
// version 2, planet_file.hpp): by default planet_<the names joined by +>.bin in the output folder
// of the tool, never the planet.bin of a project nor a file of the Unreal project; a file of other
// flags is refused (PlanetFlagsError), never built over.

#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/storage/planet_file.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace rock_tool {

// The planet file of the tool: given (--planet) if any, otherwise in directory, planet.bin without
// flags, planet_<names joined by +>.bin with the flags canonical (their canonical text).
inline std::filesystem::path planetFileOf(const std::filesystem::path& directory,
                                          const std::filesystem::path& given,
                                          const std::string& canonical) {
    if (!given.empty()) {
        return given;
    }
    if (canonical.empty()) {
        return directory / terrain::kPlanetFileName;
    }
    std::string names = canonical;
    std::replace(names.begin(), names.end(), ',', '+');
    return directory / ("planet_" + names + ".bin");
}

// The planet level of the default seed and world of params on threads threads (0: all), canonical
// the canonical text of its flags, from file (built and written there when it does not hold it);
// built tells whether it was built. Null, told on stderr, for a file of other flags.
inline std::shared_ptr<const terrain::PlanetLevel>
openPlanet(const terrain::WorldParams& world, const std::string& canonical,
           const std::filesystem::path& file, int threads, bool& built) {
    terrain::PlanetParams params = world.planet;
    params.threads = threads;
    try {
        return std::make_shared<const terrain::PlanetLevel>(
            terrain::openPlanetLevel(file, terrain::kDefaultWorldSeed, world.relief, params,
                                     terrain::WorldSize{}, &built, {}, canonical));
    } catch (const terrain::PlanetFlagsError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return nullptr;
    }
}

} // namespace rock_tool
