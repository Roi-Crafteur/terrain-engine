#pragma once

#include "cave_look.hpp"
#include "terrain/generation/cave_flora.hpp"

#include <vector>

namespace viewer {

// The cave flora of the lush districts as one indexed mesh in world meters, with a normal and a
// color per vertex, drawn without culling (the leaves are flat: they are lit from both sides).
// Every plant stays in its box, size meters high:
//  - a fern (standing on a floor at y): five flat fronds fanned out and leaning outward, dark
//    green at the foot and light green at the tip;
//  - a mushroom (standing on a floor at y): a pale stem under a cone-shaped cap in a glowing teal,
//    the cap reaching out to 0.4 * size from the axis;
//  - a vine (hanging from a ceiling at y, down to y - size): two crossed vertical strips, narrower
//    at their tip, dark green at the ceiling and light green below.
// A plant without size adds nothing.
[[nodiscard]] PaintedMesh caveFloraMesh(const std::vector<terrain::CaveFlora>& flora);

} // namespace viewer
