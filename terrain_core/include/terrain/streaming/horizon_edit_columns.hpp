#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/edit_window.hpp"

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace terrain {

// The ground of the far horizon under the player edits (phases 13 and 15). Level 0 of the
// horizon meets the tiles of the coarsest LOD sample for sample: its samples are the columns
// (x, z) = (8 i, 8 k) of their lattice. Those tiles only see the blocks within kBlockReach of
// their lattice points, whose densities the edits bring near zero (EditLattice), so that Marching
// Cubes draws an edited ground where it crosses the lattice: a razed floor shows 0.5 to 7.5 m
// above the true one, by its height modulo 8 m, and a hole between the lattice points does not
// show at all. The horizon shows what they show, so that they meet without a step.
inline constexpr int kEditColumnSpacing = 8; // tileCellMeters(kMaxLod)

// The column (x, z) = (kEditColumnSpacing x, kEditColumnSpacing z) of the lattice.
struct EditColumn {
    std::int64_t x = 0;
    std::int64_t z = 0;

    friend auto operator<=>(const EditColumn&, const EditColumn&) = default;
};

// Top of the column at (x, z), meters (multiples of kEditColumnSpacing, in the image of the
// window, whose area holds the blocks within kBlockReach of it): the vertex a tile of the
// coarsest LOD puts on its highest crossing from rock below to air above, the density of each
// lattice point y = 8 j being float(y - ground) (the generated one, ground = Heightfield::heightAt
// at the column) with the edits of the window, interpolated as marchingCubes interpolates. Within
// 1e-3 m of the vertex of the tiles (they add the offsets of their levels in floats). nullopt
// when that crossing and its two densities are the generated ones: the column keeps its ground
// bit for bit, whatever the edits elsewhere on it (tunnels under it, blocks removed in the air).
[[nodiscard]] std::optional<float> editedColumnGround(const EditWindow& window, float ground, int x,
                                                      int z);

// A column of the lattice and its edited ground.
struct EditedColumn {
    EditColumn column;           // canonical index
    std::optional<float> edited; // editedColumnGround, nullopt where the column keeps its ground
    float ground = 0.0f;         // Heightfield::heightAt, given with an edited ground only
};

// The edited blocks centered in [minX, maxX] x [minZ, maxZ], meters, any image narrower than the
// world: TerrainTiles::editWindow, or an EditWindow of a ModificationStore.
using EditWindowSource = std::function<EditWindow(int minX, int maxX, int minZ, int maxZ)>;

// The columns whose ground the edits of chunks may change, with their edited ground: the lattice
// points 16 c to 16 c + 16 of each chunk c on each axis (the chunks TerrainTiles returns from an
// edit hold every lattice point it reaches), each once, sorted by canonical index (x, then z).
// The chunks are read by groups of 32 x 32 (a region of the world format), one window each.
[[nodiscard]] std::vector<EditedColumn> editedColumns(const Heightfield& field,
                                                      std::span<const ChunkCoord> chunks,
                                                      const EditWindowSource& windows);

} // namespace terrain
