#pragma once

// The spires of rock (agent 33), internal to terrain_core: the stacks of the coasts as solids of
// their own in three dimensions, a distance field added to the relief (rock_faces.hpp: RockSpire).
// A face of the rock of the walls stands at the middle of a run and blends with the relief; a
// stack is no step of the relief: it stands as a prism of rock in beds, it may stand as two
// pillars over a pedestal or keep an arch. Each draws its own (no single model repeated), from its
// seed and its size:
// - a pillar: massive, a third narrower at its top;
// - a needle: slender, its top a third of its foot;
// - a stack split in two: two pillars of their own heights over a pedestal, a cleft between them;
// - an arch: a long pillar, a passage through it at the sea level, its bridge over it.
// Each pillar is a prism of four to seven nearly vertical faces meeting at sharp edges (never at a
// right angle), in horizontal beds (each stepping back or standing out a little, a ledge between
// them: the steps of its taper) laminated more finely, cut by one to three cracks, a notch at the
// sea level on its sea side (where the waves undercut it); its top flat and a little tilted, or
// in steps; at its foot a low rock platform on one side (in the relief, coast_reefs.cpp) and
// angular blocks fallen from it. Their details finer than a few cells fade out (the notch, the
// cracks, the laminae, the blocks, the passage of an arch narrower than two cells), as the
// overhangs of the faces do (rock_faces.hpp).

#include "terrain/generation/rock_faces.hpp"

#include <array>
#include <cstdint>

namespace terrain::rock_spires {

enum class Kind : std::uint8_t { Pillar, Needle, Split, Arch, Block };

// A spire kStackLeast meters high at least is a stack (a pillar, a needle, a split, an arch), a
// lower one a block: a low prism of four to six faces, its top a tilted slab or in steps.
inline constexpr double kStackLeast = 20.0;

inline constexpr int kMaxSides = 7;
inline constexpr int kMaxBeds = 24;
inline constexpr int kMaxBlocks = 10;

// A pillar of a spire: its axis (x, z) meters from that of the spire, its faces radius meters
// from its axis on average at the sea level (each its own share: faceReach, their normals
// (faceX, faceZ) in its frame, in order around it) and its height above it; how much narrower its
// top (taper, the power of its curve), how far it stretches along the axis of the spire (a share
// of its radius).
struct Pillar {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    double height = 0.0;
    double taper = 0.0;
    double power = 1.0;
    double stretch = 0.0;
    int sides = 6;
    std::array<double, kMaxSides> faceX{};
    std::array<double, kMaxSides> faceZ{};
    std::array<double, kMaxSides> faceReach{};
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

// A block fallen at its foot: a box halfX by halfZ meters across (in its own frame, turned by
// turns from the axes), from under the sea up to top meters over it, its top tilted by tiltX and
// tiltZ meters per meter.
struct Block {
    double x = 0.0;
    double z = 0.0;
    double halfX = 0.0;
    double halfZ = 0.0;
    double top = 0.0;
    double turns = 0.0;
    double tiltX = 0.0;
    double tiltZ = 0.0;
};

struct Shape {
    Kind kind = Kind::Pillar;
    std::array<Pillar, 3> pillars{};
    int pillarCount = 1;
    double axis = 0.0; // turns: the axis of a stretch, of a split, of an arch
    // Its beds: bed k from bedBottom[k] to bedBottom[k + 1] meters over the sea, its faces
    // standing out by bedOut[k] (receding where negative) and each face j by bedFace[k][j] more.
    int bedCount = 0;
    std::array<double, kMaxBeds + 1> bedBottom{};
    std::array<double, kMaxBeds> bedOut{};
    std::array<std::array<double, kMaxSides>, kMaxBeds> bedFace{};
    double lamina = 1.5;      // the laminae of its beds: lamina meters thick, a groove at the
    double laminaDepth = 0.0; // foot of each laminaDepth meters deep
    double notchDepth = 0.0;  // its notch at the sea level, meters deep, notchTop high
    double notchTop = 0.0;
    std::array<Crack, 3> cracks{};
    int crackCount = 0;
    double archHalf = 0.0;   // an arch: the half width of its passage along its axis
    double archHeight = 0.0; // and its height over the sea
    // Its top: a plane tilted by topTilt of its height across its radius toward tiltTurns; in steps
    // (topStep meters lower) beyond stepAt of its radius from its axis toward stepTurns.
    double topTilt = 0.0;
    double tiltTurns = 0.0;
    double topStep = 0.0;
    double stepTurns = 0.0;
    double stepAt = 0.0;
    // Its axis leans: (leanX, leanZ) meters per meter of height.
    double leanX = 0.0;
    double leanZ = 0.0;
    // Its rock platform: apron meters high at its foot toward apronTurns, apronReach meters wide
    // there, down to none on the far side (the sea at its notch there).
    double apron = 0.0;
    double apronTurns = 0.0;
    double apronReach = 0.0;
    std::array<Block, kMaxBlocks> blocks{};
    int blockCount = 0;
    double reach = 0.0; // the furthest its rock stands from its axis, meters
};

// The spires reach down to kBase meters (below the floor of the sea around them, which hides
// them there).
inline constexpr double kBase = -60.0;

// The shape of the spire of seed, radius meters wide at the sea level (its outline, its platform
// and its blocks within 1.5 of it), height meters high.
[[nodiscard]] Shape shapeOf(std::uint32_t seed, double radius, double height);

// The shape of the spire of a column (RockSpire): the last one drawn kept by each thread (the
// columns of a tile and the heights of a column read the same spire).
[[nodiscard]] const Shape& shapeOf(const RockSpire& spire);

// Its outline at the sea level toward turns, meters from its axis (its platform begins there).
[[nodiscard]] double footprintAt(const Shape& s, double turns);
// How high its platform stands at its foot toward turns, meters over the sea, and how wide it is.
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
