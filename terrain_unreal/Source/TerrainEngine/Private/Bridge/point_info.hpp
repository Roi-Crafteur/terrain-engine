#pragma once

#include "tile_streamer.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace terrain::unreal {

// What the tools of the developers say of a point (agent 36: the pages of F3, Terrain.Info): lines
// of text read from the core, never written to it. The core decides, the screen shows.

// One line: its section (the module it comes from: "ground", "water", "planet"...), its label
// and its value, in English like the log.
struct InfoLine {
    std::string section;
    std::string label;
    std::string value;

    friend bool operator==(const InfoLine&, const InfoLine&) = default;
};

// The point asked about: a column (x, z), meters of the core, any image; and for a point hit in
// three dimensions (the point aimed at) its altitude y. The world of the session: its
// heightfield (with its hydrology, if any) and its ecology; the streamer of its tiles, if any.
struct InfoQuery {
    const Heightfield& field;
    const Ecology& ecology;
    const TileStreamer* streamer = nullptr;
    double x = 0.0;
    double z = 0.0;
    std::optional<double> y;
};

// A provider adds the lines of its module about the point; nothing when it has nothing to say.
using InfoProvider = std::function<void(const InfoQuery& query, std::vector<InfoLine>& lines)>;

// The providers of this version, in their order: the position (canonical point, chunk, block,
// cell of the planet), the ground and the water, the relief before the rivers, the biome, the
// material and the climate, the planet level, the valleys and lakes of the rivers, the nearest
// fall, the coast, the surface (on the heightmap or off it: 3D rock, a cave, an edit) and the
// tile shown. A module adds its lines (the type of the coast, the 3D rock of phase 12A, the
// kingdoms) with one more function of the form of InfoProvider, appended to this list in
// point_info.cpp: the screen of F3 and Terrain.Info show them with no other change.
[[nodiscard]] std::vector<InfoProvider> defaultInfoProviders();

// The lines of every provider about the point, in their order.
[[nodiscard]] std::vector<InfoLine> describePoint(const InfoQuery& query,
                                                  std::span<const InfoProvider> providers);

// The name of a kind of water: "none", "sea", "river", "lake", "salt lake".
[[nodiscard]] const char* waterKindName(WaterKind kind);

} // namespace terrain::unreal
