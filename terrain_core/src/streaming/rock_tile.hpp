#pragma once

// The rock of a streamed tile in three dimensions (phase 12A), internal to terrain_tiles.cpp and
// cave_tiles.cpp: the density of its levels where faces lie at its columns (rock_faces.hpp), the
// heights its rock reaches, its plants and the rock of its vertices.

#include "terrain/generation/rock_faces.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/lod.hpp"

#include "tile_lattice.hpp"

#include <vector>

namespace terrain::tile {

class RockTile {
public:
    // columns hold their rock (sampleColumns with rock, a face at one of them at least) and must
    // outlive the tile.
    RockTile(const TileColumns& columns, const TileRequest& request, const TileLattice& lattice);

    // The density of sample (i, j, k) of the level whose lowest samples lie at originY, with the
    // detail of the tile, or of its coarser neighbor (the samples it holds on a stitched side):
    // y - ground bit for bit at the columns without a face, as tile::levelDensity, and on the
    // sides the horizon meets (TileRequest::horizon) and beyond them. With the ring of the
    // columns (TileColumns::ring), i or k may lie one step beyond the tile.
    [[nodiscard]] float sample(int i, int j, int k, int originY, bool coarse) const;
    // The same with a detail given.
    [[nodiscard]] float sampleWith(int i, int j, int k, int originY,
                                   const RockDetail& detail) const;

    // The detail of the rock at column (i, k): the tile's, turning to its coarser neighbor's
    // over a few cells toward a stitched side, that one's on the side (the samples the coarser
    // tile holds there).
    [[nodiscard]] RockDetail detailAt(int i, int k) const;

    // The density of a level, each column with its detail (detailAt), before the block edits and
    // the stitching.
    [[nodiscard]] DensityGrid levelDensity(int originY) const;

    // True if the detail of the coarser neighbor differs from the tile's on a stitched side.
    [[nodiscard]] bool resamplesSeams() const { return resample_; }

    // The heights between which the rock of the tile lies.
    [[nodiscard]] RockBounds bounds() const { return bounds_; }

    // Drops the plants standing on a column with a face: the rock may lie above them or carve
    // the ground under them.
    void dropPlants(std::vector<Plant>& plants) const;

    // Sets the normals of the mesh of a level (in cells, as Marching Cubes gives it) whose density
    // is grid and lowest samples lie at originY: centered differences of the density everywhere,
    // through the rock above and below the level and, with the ring of the columns, beyond the
    // sides of the tile (the normals the neighboring tile of the same LOD gives there, so that a
    // face of rock keeps its light across the levels and the tiles); without it (the tiles of the
    // caves), second order differences across the sides. Then agreeWithTriangles
    // (tile_normals.hpp) is left to the caller, as for every tile.
    void lightLevel(Mesh& level, const DensityGrid& grid, int originY) const;

    // The share of rock of each vertex of a mesh of the tile whose origin lies at originY: that of
    // the nearest column (rockShare).
    [[nodiscard]] std::vector<float> vertexRock(const Mesh& mesh, double originY) const;

private:
    [[nodiscard]] const RockColumn& rockAt(int i, int k) const;
    [[nodiscard]] bool hasRing() const { return !columns_->ring.empty(); }
    // The column (i, k), i or k one step beyond the tile on the ring; its ground.
    [[nodiscard]] const RockColumn& columnAt(int i, int k) const;
    [[nodiscard]] float groundAt(int i, int k) const;
    [[nodiscard]] Vec3f gradientAt(const DensityGrid& grid, int i, int j, int k,
                                   int originY) const;
    [[nodiscard]] bool onHorizonSide(int i, int k) const;

    const TileColumns* columns_;
    TileLattice lattice_;
    unsigned seams_;
    unsigned horizon_;
    RockDetail fine_;
    RockDetail coarse_;
    bool resample_ = false;
    RockBounds bounds_;
};

} // namespace terrain::tile
