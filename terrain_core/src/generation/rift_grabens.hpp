#pragma once

// The grabens of the rifts (agent 52), internal to terrain_core: troughs sunk below the sea level
// along the axes of the continental rifts (the Dead Sea, Lake Assal, the Danakil), deep under the
// floor of the rift around them. Placed by the geography alone: on the nodes of its map on the
// axis of a rift (kRift at least 0.9), about one every 800 km of axis, at least 600 km apart,
// their long axis along the rift. Each is 80 to 250 km long and 15 to 40 km wide, its floor 100
// to 450 m below the sea level (most of them shallower), flat but for a share of the relief of the
// rift; its long sides fault walls in 2 to 4 steps (steep faces over benches tilted back), its
// ends tapering. Accepted only where the floor of the rift stands at most 500 m high and its
// whole footprint lies in the trough, on land without relief (no mountain: they keep their bits)
// and far from the sea. The climate of the planet level fills them or not (salt lakes in the dry
// belts). Pure arithmetic of doubles, noises and hashes: the same bits on every platform,
// periodic on the world.

#include "low_reach.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <cstdint>
#include <vector>

namespace terrain::lows {

struct Graben {
    double x = 0.0; // canonical center
    double z = 0.0;
    double axisX = 1.0; // unit direction of the long axis
    double axisZ = 0.0;
    double length = 1.0; // half length, meters
    double width = 1.0;  // half width
    double floor = -1.0; // the altitude its floor aims at
    int steps = 2;       // of each wall
    double faceShare = 0.1; // of a step taken by its face
    double reach = 1.0;
};

struct Grabens {
    noise::FbmOctaves warp;  // bends the axis and the walls
    noise::FbmOctaves widths; // how wide the trough is along it
    noise::FbmOctaves steps;  // where the faults of the steps run
    std::vector<Graben> sites;
    ReachLattice reach;
};

[[nodiscard]] Grabens makeGrabens(std::uint64_t seed, const Geography& geography, double density);

// How the ground of the grabens lies at the canonical (x, z): the share of the way down to its
// floor (1 on the floor, 0 beyond the rim) and the floor (the deepest share among those reaching).
struct GrabenPlace {
    double share = 0.0;
    double floor = 0.0;
};
[[nodiscard]] GrabenPlace grabenAt(const Grabens& grabens, const WorldSize& world, double x,
                                   double z);

} // namespace terrain::lows
