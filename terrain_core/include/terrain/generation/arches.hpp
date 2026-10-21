#pragma once

#include "terrain/generation/cliff_region.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// Lengths and heights are in meters. An arch is a vertical half-torus standing on two pillars,
// with its opening carved below it, placed on flat rock of the cliffs (see placeArches).
struct ArchParams {
    // Candidates: one per cell of a world grid of spacing meters, at the center of the cell
    // moved by up to jitter / 2 of the cell along each axis, kept with probability chance.
    double spacing = 400.0;
    double jitter = 0.5;
    float chance = 0.5f;

    // Shape: the half-span (center to pillar axis) is drawn in [minHalfSpan, maxHalfSpan].
    float minHalfSpan = 18.0f;
    float maxHalfSpan = 30.0f;
    float tubeRadius = 5.0f;        // of the half-torus
    float pillarRadius = 6.0f;      // at least tubeRadius, so the tube rests on the pillars
    float pillarHeight = 6.0f;      // springline of the half-torus above the base
    float anchorDepth = 12.0f;      // the pillars go this deep into the rock below the base
    float openingHalfDepth = 6.0f;  // the opening is carved this far across the arch

    // Placement: cliff mask at least minCliffMask and rock surface level to maxGroundRelief,
    // sampled every groundStep meters over the footprint (pillars and opening).
    float minCliffMask = 0.9f;
    float maxGroundRelief = 4.0f;
    double groundStep = 5.0;
    // Columns farther than the bounding radius plus reachMargin from the center ignore the
    // arch: there its density is at least reachMargin, so it would only change the air.
    float reachMargin = 48.0f;
};

// An arch of the world. The span runs along the unit direction, the pillars stand at
// center +- halfSpan * direction, on the rock at height base.
struct Arch {
    double x = 0.0;
    double z = 0.0;
    double directionX = 1.0;
    double directionZ = 0.0;
    float halfSpan = 0.0f;
    float base = 0.0f;
    int cellX = 0; // jittered grid cell of the candidate
    int cellZ = 0;

    friend bool operator==(const Arch&, const Arch&) = default;
};

// Deterministic candidates and signed distance fields of the arches: the same seed,
// parameters and cell always give the same candidate, wherever the region is. The grid of the
// candidates tiles the world: round(world / spacing) cells per side (PeriodicCells).
class ArchField {
public:
    // Throws std::invalid_argument for an invalid world (validate).
    explicit ArchField(std::uint64_t seed, const ArchParams& params = {},
                       const WorldSize& world = {});

    // Candidate of the grid cell (cellX, cellZ), an unwrapped index, which spans
    // [cellX, cellX + 1) * the cell size along x and the same along z, with base 0; false if
    // the cell has none (chance). Drawn from the canonical cell, at the image of its position
    // next to the cell.
    [[nodiscard]] bool candidate(int cellX, int cellZ, Arch& arch) const;

    // Heights of the springline (top of the pillars), of the crown of the half-torus and of
    // the top of its opening.
    [[nodiscard]] float springline(const Arch& arch) const;
    [[nodiscard]] float crown(const Arch& arch) const;
    [[nodiscard]] float openingTop(const Arch& arch) const;

    // Horizontal radius around the center that holds the arch and its opening, and the reach
    // beyond which a column ignores the arch.
    [[nodiscard]] double boundingRadius(const Arch& arch) const;
    [[nodiscard]] double reach(const Arch& arch) const;

    // Rock of the arch: half-torus (above the springline) and pillars.
    [[nodiscard]] float rock(const Arch& arch, double x, double y, double z) const;
    // Opening carved below the arch: the half-disk inside the half-torus and the space
    // between the pillars down to the base, extruded across the arch by openingHalfDepth.
    [[nodiscard]] float opening(const Arch& arch, double x, double y, double z) const;
    // The terrain density with the opening carved out and the arch added.
    [[nodiscard]] float apply(const Arch& arch, float terrain, double x, double y,
                              double z) const;

    [[nodiscard]] const ArchParams& params() const { return params_; }
    [[nodiscard]] const PeriodicCells& cellsX() const { return cellsX_; }
    [[nodiscard]] const PeriodicCells& cellsZ() const { return cellsZ_; }

private:
    ArchParams params_;
    PeriodicCells cellsX_;
    PeriodicCells cellsZ_;
    std::uint64_t seed_;
};

// Arches of a cliff region: the candidates of the grid cells covering it, in row-major cell
// order, kept when their whole reach lies inside the region (away from its border ring),
// the cliff mask is at least minCliffMask and the rock surface (CliffRegion::surfaceAt)
// level within maxGroundRelief over the footprint, no river, ocean, cascade or cirque is
// within reach and no arch kept before is within the sum of their reaches. The base is the
// lowest rock surface of the footprint. Deterministic.
[[nodiscard]] std::vector<Arch> placeArches(const CliffRegion& cliffs, const ArchField& field);

} // namespace terrain
