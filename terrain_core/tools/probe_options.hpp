#pragma once

// The command line of terrain_probe (probe.cpp): everything is checked before a planet file is
// read or built, an invalid argument named on stderr.

#include "terrain/generation/heightfield.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace probe {

// --line x1 z1 x2 z2 step: the points every step meters from (x1, z1), then (x2, z2) itself.
struct Line {
    double x1 = 0.0;
    double z1 = 0.0;
    double x2 = 0.0;
    double z2 = 0.0;
    double step = 1.0;
};

struct Options {
    std::filesystem::path planet;      // --planet: read, or built and written when stale
    std::filesystem::path planetCheck; // --planet-check: read only, never written
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    int threads = 0;
    double radius = 6.0;   // meters: the vertices of the tiles around a point (render)
    double falls = 1000.0; // meters: the lips of the falls around a point (falls)
    bool caves = false;    // the caves of the default parameters carved (none by default)
    bool json = false;
    bool listSections = false;
    std::vector<std::string> sections; // --sections a,b: empty for the defaults
    std::vector<std::pair<double, double>> points;
    std::optional<Line> line;
};

// At most this many points along a line.
inline constexpr double kMaxLinePoints = 100'000.0;

// The options of argv, false with a message on stderr ("Invalid <what> '<text>'") for an
// invalid one; known: the names of the sections (--sections must name some of them).
[[nodiscard]] bool parseOptions(int argc, char** argv, const std::vector<std::string>& known,
                                Options& out);

// The usage line of the tool.
[[nodiscard]] std::string usage(const char* tool);

} // namespace probe
