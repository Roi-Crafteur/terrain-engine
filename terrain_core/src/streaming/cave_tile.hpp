#pragma once

// Caves of a streamed tile (internal to terrain_tiles.cpp and cave_tiles.cpp).

#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "tile_lattice.hpp"

#include <optional>
#include <vector>

namespace terrain::tile {

// Depth below the lowest ground of a tile down to which its levels are meshed with caves: the
// deepest cave surface lies at most maxDepth plus the blend below the ground of its column.
[[nodiscard]] double caveDepthReach(const CaveField& field, const TileLattice& lattice);

// Drops the plants of the tile standing within 2 m of an entrance, horizontally: the ground
// under them may be carved.
void dropPlantsOverEntrances(const CaveField& field, const CaveGround& ground,
                             const TileLattice& lattice, std::vector<Plant>& plants);

// Caves of the levels of a tile: the samples are carved with the detail of its LOD, and the
// even samples of the sides it stitches to a coarser neighbor (faces and corner columns) with
// the detail of that LOD, so that they hold the values the coarser tile samples there.
class CaveTile {
public:
    CaveTile(const CaveField& field, const CaveGround& ground, const TileRequest& request,
             const TileLattice& lattice, const TileColumns& columns,
             const std::vector<int>& levels);

    // Carved density of the level whose lowest samples lie at originY, before the block edits
    // and the stitching.
    [[nodiscard]] DensityGrid levelDensity(int originY);

    // True if the detail of the tile carves the aquifers: its lakes are meshed.
    [[nodiscard]] bool holdsWater() const;
    // Water density of the level (CaveField::waterDensity) over its final carved density, with
    // the edits and the stitching; on the sides stitched to a coarser tile without lakes, no
    // water, so that the lakes close there.
    [[nodiscard]] DensityGrid waterDensity(const DensityGrid& carved, int originY);

    // Materials of the vertices of the terrain and of the lakes of the tile (origin of data).
    void paint(TileData& data, const CaveEnvironmentField& environment,
               const CaveMaterialParams& params);
    // Speleothems whose candidates lie on the tile (LOD 0 tiles: the detail carves everything).
    [[nodiscard]] std::vector<Speleothem> speleothems(const SpeleothemField& field);

private:
    struct Carving {
        CaveSampler sampler;
        std::vector<CaveColumn> columns; // x varies fastest
    };
    [[nodiscard]] Carving carving(const CaveField& field, const CaveGround& ground,
                                  const CaveDetail& detail, const CaveBox& box) const;
    void resampleSeams(DensityGrid& grid, int originY);
    [[nodiscard]] float sample(Carving& carving, int i, int j, int k, int originY) const;
    // True if the sample (i, k) of a level lies on a side stitched to a coarser tile.
    [[nodiscard]] bool onStitchedSide(int i, int k) const;
    // Ground of the tile at (x, z), bilinear between its columns.
    [[nodiscard]] float groundAt(double x, double z) const;

    TileLattice lattice_;
    const TileColumns* columns_;
    unsigned seams_;
    Carving fine_;
    std::optional<Carving> coarse_; // only when a stitched side carves another detail
};

} // namespace terrain::tile
