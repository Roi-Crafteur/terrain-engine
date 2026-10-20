#pragma once

#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <cstdint>

namespace terrain {

struct CaveRegionParams {
    ErodedRegionParams eroded;
    CaveParams caves;
    EcologyParams ecology; // its climate and biomes, as in the streamed world
};

// Caves under an eroded region: their ground is the eroded relief (ErodedRegion::heightAt,
// the heightfield outside the region) and its rivers keep them away; their environment (biome
// and climate above, for the aquifers, the speleothems and the materials) is the one of the
// heightfield (CaveEnvironmentField). The streamed world carves the same caves under the
// heightfield, which has no river. Deterministic.
class CaveRegion {
public:
    CaveRegion(std::uint64_t seed, const HeightmapRegion& region,
               const CaveRegionParams& params = {}, const WorldSize& world = {});

    // The ground callbacks point into the region: it cannot be copied or moved.
    CaveRegion(const CaveRegion&) = delete;
    CaveRegion& operator=(const CaveRegion&) = delete;

    [[nodiscard]] float groundAt(double x, double z) const { return eroded_.heightAt(x, z); }
    // Horizontal distance to the nearest river sample, meters: the bilinear interpolation of
    // the chamfer distances of the surface samples, clamped to the border of the region.
    [[nodiscard]] float riverDistanceAt(double x, double z) const;

    // Density of the eroded relief (y - groundAt, as a float) with the caves of the detail.
    [[nodiscard]] float densityAt(double x, double y, double z,
                                  const CaveDetail& detail = {}) const;
    // Sampler of the caves of the region over a box.
    [[nodiscard]] CaveSampler sampler(const CaveBox& box, const CaveDetail& detail = {}) const;

    [[nodiscard]] const ErodedRegion& eroded() const { return eroded_; }
    [[nodiscard]] const TerrainSurface& surface() const { return surface_; }
    [[nodiscard]] const CaveField& field() const { return field_; }
    [[nodiscard]] const CaveGround& ground() const { return ground_; }
    [[nodiscard]] const CaveEnvironmentField& environment() const { return environment_; }
    [[nodiscard]] const SpeleothemField& speleothems() const { return speleothems_; }

private:
    ErodedRegion eroded_;
    TerrainSurface surface_;
    CaveField field_;
    Ecology ecology_;
    CaveEnvironmentField environment_;
    SpeleothemField speleothems_;
    CaveGround ground_;
};

} // namespace terrain
