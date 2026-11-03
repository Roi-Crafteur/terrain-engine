#pragma once

// The spires of rock (agent 33), internal to terrain_core: the stacks of the coasts as solids of
// their own in three dimensions, a distance field added to the relief (rock_faces.hpp: RockSpire).
// A face of the rock of the walls stands at the middle of a run and blends with the relief; a
// stack is no step of the relief: it narrows toward its top, its outline turns all around, it may
// stand as two pillars over a pedestal or keep an arch. Each draws its own (no single model
// repeated), from its seed and its size:
// - a pillar: massive, a quarter narrower at its top;
// - a needle: slender, its top a third of its foot;
// - a stack split in two: two pillars of their own heights over a pedestal, a cleft between them;
// - an arch: a long pillar, a passage through it at the sea level, its bridge over it.
// All of them stand in beds (benches: each bed leans back over its height and stands out or
// recedes by its own draw), their outline irregular (five harmonics, the arches stretched), cut by
// one to three cracks, a notch at the sea level (where the waves undercut them), their top rough;
// a scree apron at their foot on one side (in the relief, coast_reefs.cpp). Their details finer
// than a few cells fade out (the notch, the cracks, the beds, the passage of an arch narrower than two cells),
// as the overhangs of the faces do (rock_faces.hpp).

#include "terrain/generation/rock_faces.hpp"

#include <array>
#include <cstdint>

namespace terrain::rock_spires {

enum class Kind : std::uint8_t { Pillar, Needle, Split, Arch };

// A pillar of a spire: its axis (x, z) meters from that of the spire, its radius at the sea level
// and its height above it, how much narrower its top (taper, the power of its curve), how far it
// stretches along the axis of the spire (a share of its radius), whether its top is rough.
struct Pillar {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    double height = 0.0;
    double taper = 0.0;
    double power = 1.0;
    double stretch = 0.0;
};

// A crack: toward turns, width meters wide along the outline, depth meters deep, from low to high
// meters above the sea, wandering by up to wander turns around it and narrowing and widening as
// it rises (its phase).
struct Crack {
    double turns = 0.0;
    double width = 0.0;
    double depth = 0.0;
    double low = 0.0;
    double high = 0.0;
    double wander = 0.0;
    double phase = 0.0;
};

struct Shape {
    Kind kind = Kind::Pillar;
    std::array<Pillar, 3> pillars{};
    int pillarCount = 1;
    std::array<double, 5> lobe{};  // the harmonics 2 to 6 of its outline, shares of the radius
    std::array<double, 5> phase{}; // their phases, turns
    double axis = 0.0;             // turns: the axis of a stretch, of a split, of an arch
    double bed = 6.0;              // the height of its beds, meters
    double bedLean = 0.0;          // each bed leans back over its height by this, meters
    double bedJitter = 0.0;        // a bed stands out or recedes by up to this, meters
    std::uint64_t bedSeed = 0;
    double notchDepth = 0.0;       // its notch at the sea level, meters deep, notchTop high
    double notchTop = 0.0;
    std::array<Crack, 3> cracks{};
    int crackCount = 0;
    double archHalf = 0.0;   // an arch: the half width of its passage along its axis
    double archHeight = 0.0; // and its height over the sea
    double topRough = 0.0;   // its top rises and dips by this share of its height
    double topTilt = 0.0;    // and leans by this share of it across, toward tiltTurns
    double tiltTurns = 0.0;
    // It bulges and narrows by up to bulge of its radius, over bulgeWave meters of its height
    // (two waves), the bulges turning around it (bulgePhase).
    double bulge = 0.0;
    std::array<double, 2> bulgeWave{};
    std::array<double, 4> bulgePhase{};
    // Its axis leans: (leanX, leanZ) meters per meter of height.
    double leanX = 0.0;
    double leanZ = 0.0;
    // Its scree apron: apron meters high at its foot toward apronTurns, down to none on the far
    // side (the sea at its notch there).
    double apron = 0.0;
    double apronTurns = 0.0;
    double reach = 0.0; // the furthest its rock stands from its axis, meters
};

// The scree apron slopes kApronSlope meters per meter from its top down to the sea level (its
// flank 0.7 at most, rounded at both ends: coast_reefs::domeOf; the slope law of the tiles).
inline constexpr double kApronSlope = 0.5;
// The spires reach down to kBase meters (below the floor of the sea around them, which hides
// them there).
inline constexpr double kBase = -60.0;

// The shape of the spire of seed, radius meters wide at the sea level (its outline and its apron
// within 1.4 of it), height meters high.
[[nodiscard]] Shape shapeOf(std::uint32_t seed, double radius, double height);

// The shape of the spire of a column (RockSpire): the last one drawn kept by each thread (the
// columns of a tile and the heights of a column read the same spire).
[[nodiscard]] const Shape& shapeOf(const RockSpire& spire);

// Its outline at the sea level toward turns, meters from its axis (its apron begins there).
[[nodiscard]] double footprintAt(const Shape& s, double turns);
// How high its apron stands at its foot toward turns, meters over the sea, and how wide it is.
[[nodiscard]] double apronAt(const Shape& s, double turns);
[[nodiscard]] double apronWidth(const Shape& s, double turns);
// The highest it stands, meters over the sea.
[[nodiscard]] double summitOf(const Shape& s);

// The density of its rock at (dx, dz) meters from its axis, y meters over the sea level, as cells
// of cell meters draw it: negative in the rock, about the distance to its surface.
[[nodiscard]] double densityAt(const Shape& s, double dx, double dz, double y, double cell);

// The highest its rock stands at (dx, dz) as cells of cell meters draw it, meters over the sea;
// below kBase where it holds none.
[[nodiscard]] double topAt(const Shape& s, double dx, double dz, double cell);

// The highest it stands within half meters of (dx, dz) along x and z (a cell of the horizon around
// its sample, as its crests: a needle narrower than the cell still shows), as cells of cell meters
// draw it: at the point, the tops of its pillars brought into the square, and a lattice of it.
[[nodiscard]] double topWithin(const Shape& s, double dx, double dz, double half, double cell);

} // namespace terrain::rock_spires
