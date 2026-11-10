#pragma once

// The player edits in the levels of a streamed tile (phase 15, agent 41), internal to
// terrain_tiles.cpp: the levels the edited blocks reach, the edited density of a level, and the
// normals of the surfaces the edits made.

#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/storage/edit_coarse.hpp"
#include "terrain/storage/edit_window.hpp"
#include "tile_lattice.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace terrain::tile {

// Meters from the lattice of a tile to the farthest block its edited density reads: kBlockReach
// at LOD 0, CoarseEdits::reach of its spacing beyond; on its stitched sides, the reach of the
// lattice of the coarser LOD (seams: TileSeam bits).
[[nodiscard]] int editReach(int lod, unsigned seams);

// Levels holding the ground of the columns, the caves down to caveDepth below it, or a lattice
// point an edited block reaches (within reach meters of it).
[[nodiscard]] std::vector<int> levelsToMesh(const TileColumns& columns, const EditWindow& edits,
                                            int height, double caveDepth, int reach);

// True if the edits leave no surface in the level: every block within reach of its samples lies
// in filled sections of one edit, or the level lies above the ground without an added block
// within reach (all air), or under it without a removed one (all rock). Tiles without caves
// only: the caves carve the rock.
[[nodiscard]] bool filledLevel(const EditWindow& edits, const TileLattice& lattice,
                               const TileColumns& columns, int originY, int reach);

// The edits of a level: the blocks around each of its samples (LOD 0) or the cells of its
// lattices (beyond), and how they changed each sample (kRaised by removed blocks, kLowered by
// added ones, kKept otherwise; kRelayRaised and kRelayLowered when a relay of CoarseEdits set
// it), sample (i, j, k) at (k * kGridSamples + j) * kGridSamples + i.
struct LevelEdits {
    static constexpr std::int8_t kKept = 0;
    static constexpr std::int8_t kRaised = 1;
    static constexpr std::int8_t kLowered = -1;
    static constexpr std::int8_t kRelayRaised = 2;
    static constexpr std::int8_t kRelayLowered = -2;

    std::optional<EditLattice> blocks; // LOD 0 only
    std::optional<CoarseEdits> coarse; // beyond LOD 0
    std::optional<CoarseEdits> sides;  // the stitched sides, as the coarser LOD
    std::vector<std::int8_t> changes;
    int spacing = 1;
    unsigned seams = 0;
};

// Applies the edits to the density of a level, every added block first, then every removed one:
// block by block at LOD 0 (ModificationStore::editedDensity), by the cells of the coarse lattices
// beyond (CoarseEdits); the even samples of the stitched sides (seams) as the coarser LOD gives
// them, which stitchSeams needs. Returns what they changed, or nothing when no edited block
// reaches the level (the grid kept bit for bit).
[[nodiscard]] std::optional<LevelEdits> applyBlocks(DensityGrid& grid, const TileLattice& lattice,
                                                    int originY, const EditWindow& edits, int lod,
                                                    unsigned seams);

// The normals of the vertices of a level (its mesh in cells, lit as the tile lights it) on an
// edge with a sample the edits changed. At LOD 0 they turn toward the gradient of the smoothed
// share of the edited blocks around those samples (edit_cubes::shareGradient); beyond, on an
// edge with a sample a relay set, toward the face of its nearest box (CoarseEdits::
// relayDirection), so that a wall of one block keeps its upright face and its rock. Both in
// proportion to the part of the edge on the side of those samples. The positions keep the 1 m of
// the blocks; their light turns smoothly over the steps and corners of the blocks, the same on
// both sides of a tile. Every other vertex keeps its normal bit for bit.
void lightEdits(Mesh& level, const LevelEdits& edits);

} // namespace terrain::tile
