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
    // The same with share of the detail of the tile, the rest of its coarser neighbor's.
    [[nodiscard]] float sampleBlended(int i, int j, int k, int originY, double share) const;

    // How much of the detail of the tile the rock takes at column (i, k): all of it, but over a
    // few cells toward a stitched side, where it turns to the coarser neighbor's, none on the side
    // (the samples the coarser tile holds there). The densities of both details are blended
    // there: a detail between them would draw features neither tile draws (a blade of rock along
    // the side, where a cap fades out).
    [[nodiscard]] double fineShare(int i, int k) const;

    // The density of a level, each column blending the densities of both details by its share
    // (fineShare), before the block edits and the stitching.
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
    // caves), second order differences across the sides. Each averaged with those of the lattice
    // points just above and below (beyond the level through the rock, as the level next to it
    // takes them): a feature of the rock a few cells high (the lip of a cap) crossing the rows of
    // the lattice along a face whose top climbs steeply changes shape from one column to the next,
    // and its light would break into teeth along the fold. Then agreeWithTriangles
    // (tile_normals.hpp) is left to the caller, as for every tile.
    void lightLevel(Mesh& level, const DensityGrid& grid, int originY) const;

    // The share of rock of each vertex of a mesh of the tile whose origin lies at originY: that of
    // the nearest column (rockShare), its details blended as its density (fineShare).
    [[nodiscard]] std::vector<float> vertexRock(const Mesh& mesh, double originY) const;

private:
    [[nodiscard]] const RockColumn& rockAt(int i, int k) const;
    [[nodiscard]] bool hasRing() const { return !columns_->ring.empty(); }
    // The column (i, k), i or k one step beyond the tile on the ring; its ground.
    [[nodiscard]] const RockColumn& columnAt(int i, int k) const;
    [[nodiscard]] float groundAt(int i, int k) const;
    // The density of sample (i, j, k) of a level: the grid's within it (with its edits), the
    // rock's beyond it (above, below and, with the ring, beyond its sides).
    [[nodiscard]] float valueAt(const DensityGrid& grid, int i, int j, int k, int originY) const;
    // The gradient of the density at lattice point (i, j, k) of a level, j one row beyond it at
    // most.
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
