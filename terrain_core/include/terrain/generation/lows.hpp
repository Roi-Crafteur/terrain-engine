#pragma once

// The lows of the relief (agent 52): ground that goes down below the land around it, often below
// the sea level, never the sea unless the sea reaches it (the geography decides the sea, never the
// height). Placed by the geography (the plains, the troughs of the rifts) and the seed, never by
// the biome; the planet level and the rivers are built over them, so its climate decides their
// water (dry, salt lake or lake). Families:
// - depressions: those of phase 11B, deep in the plains (depressions.hpp), their bits kept;
// - basins: more of them, smaller (Qattara, Death Valley), on the forms of the depressions;
// - grabens: sunken troughs along the axes of the rifts (the Dead Sea), fault walls in steps;
// - craters: rare impact craters, placed by the seed alone, most of them worn (the humidity of
//   the pre-climate, the softness of the rock), their rims and ejecta raised over the plains.
// Out of the reach of every low, the relief keeps every bit (lows_outside_test).

#include <cstdint>

namespace terrain {

enum class LowKind : std::uint8_t { Depression, Basin, Graben, Crater };
inline constexpr int kLowKindCount = 4;
[[nodiscard]] const char* lowKindName(LowKind kind);

// A low of the relief: its center (canonical), its mean radius (a graben: its half length; a
// crater: that of the crest of its rim), how far its ground may move from the center, the
// altitude its floor aims at (a crater: how far its floor lies under the ground around it, below
// 0), the direction of its long axis (turns from +x toward +z) and its wear (a crater: 0 fresh, 1
// worn out).
struct LowSite {
    LowKind kind = LowKind::Depression;
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    double reach = 0.0;
    float floor = 0.0f;
    double axis = 0.0;
    float wear = 0.0f;

    friend bool operator==(const LowSite&, const LowSite&) = default;
};

// What the Heightfield lays of the lows (HeightfieldParams::lows), the depressions of phase 11B
// aside (GeographyParams::depressions). Off: the relief of before them, bit for bit. density
// multiplies the share of the cells that hold a low (the small worlds of the tests). Off by
// default while their settings are being chosen (work in progress, agent 52): the world keeps
// every bit until they are on.
struct LowsParams {
    bool enabled = false;
    bool basins = true;
    bool grabens = true;
    bool craters = true;
    double density = 1.0;
};

struct HeightfieldParams;
struct PlanetParams;

// Switches every low on (the experimental flag "lows", experimental_flags.hpp): the families of
// LowsParams. Off by default: the world of main, bit for bit.
void lowsOn(HeightfieldParams& relief, PlanetParams& planet);

} // namespace terrain
