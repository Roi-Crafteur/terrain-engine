#pragma once

// Caves of a streamed tile (internal to terrain_tiles.cpp and cave_tiles.cpp).

#include "terrain/generation/cave_flora.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "rock_tile.hpp"
#include "tile_lattice.hpp"

#include <optional>
#include <vector>

namespace terrain::tile {

enum class CaveFluid { Water, Lava };

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
    // rock: the rock of the tile in three dimensions under which the caves are carved, or null
    // for the relief alone (y - ground); it must outlive the tile.
    CaveTile(const CaveField& field, const CaveGround& ground, const TileRequest& request,
             const TileLattice& lattice, const TileColumns& columns,
             const std::vector<int>& levels, const RockTile* rock = nullptr);

    // Carved density of the level whose lowest samples lie at originY, before the block edits
    // and the stitching.
    [[nodiscard]] DensityGrid levelDensity(int originY);

    // True if the detail of the tile carves the aquifers: its lakes are meshed.
    [[nodiscard]] bool holdsWater() const;
    // Density of the water (CaveField::waterDensity: lakes and rivers) or of the lava
    // (CaveField::lavaDensity) of the level over its final carved density, with
    // the edits and the stitching; on the sides stitched to a coarser tile without aquifers, no
    // fluid, so that the lakes, rivers and lava close there.
    [[nodiscard]] DensityGrid fluidDensity(CaveFluid fluid, const DensityGrid& carved,
                                           int originY);

    // Decorates the meshed tile, never changing its geometry: the materials of the vertices of
    // its terrain and lakes (origin of data), then the speleothems and the cave flora whose
    // candidates lie on the tile, from the fields given (LOD 0 tiles: the detail carves
    // everything; null for none).
    void decorate(TileData& data, const CaveEnvironmentField& environment,
                  const CaveMaterialParams& params, const SpeleothemField* speleothems,
                  const CaveFloraField* flora);

private:
    struct Carving {
        CaveSampler sampler;
        std::vector<CaveColumn> columns; // x varies fastest
    };
    [[nodiscard]] Carving carving(const CaveField& field, const CaveGround& ground,
                                  const CaveDetail& detail, const CaveBox& box) const;
    void resampleSeams(DensityGrid& grid, int originY);
    [[nodiscard]] float sample(Carving& carving, int i, int j, int k, int originY);
    // True if the sample (i, k) of a level lies on a side stitched to a coarser tile.
    [[nodiscard]] bool onStitchedSide(int i, int k) const;
    // Ground of the tile at (x, z), bilinear between its columns.
    [[nodiscard]] float groundAt(double x, double z) const;
    void paint(TileData& data, const CaveEnvironmentField& environment,
               const CaveMaterialParams& params);
    // True if the river field of the profile reaches the threshold of the water.
    [[nodiscard]] bool riverAt(const CaveProfile& profile) const;
    // The wall at (x, y, z) next to the water of a river: its height above it.
    void markRiver(CaveWallSite& site, double x, double y, double z) const;

    TileLattice lattice_;
    const TileColumns* columns_;
    const RockTile* rock_;
    unsigned seams_;
    Carving fine_;
    std::optional<Carving> coarse_; // only when a stitched side carves another detail
};

} // namespace terrain::tile
