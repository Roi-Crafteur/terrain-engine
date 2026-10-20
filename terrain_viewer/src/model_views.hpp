#pragma once

#include "chunk_mesh.hpp"
#include "colored_mesh.hpp"
#include "math.hpp"

#include <optional>

namespace viewer {

struct AltitudeRange {
    float low;
    float high;
};

struct HeightfieldRegion;

// Region of the multi-scale heightfield, generated on first use: meshing it takes a moment.
struct HeightfieldView {
    explicit HeightfieldView(const HeightfieldRegion& region);

    ChunkMesh mesh;
    AltitudeRange altitudes;
    Vec3 viewpoint;
};

// A region as a scale model (terrain mesh, water triangles), generated on first use. Region
// is one of the region views: ErodedRegionView, CliffRegionView or SculptedRegionView.
struct ModelView {
    template <class Region>
    explicit ModelView(const Region& region)
        : terrain(region.terrain), water(region.water),
          altitudes{region.minHeight, region.maxHeight}, viewpoint(region.viewpoint) {}

    ChunkMesh terrain;
    ColoredMesh water;
    AltitudeRange altitudes;
    Vec3 viewpoint;
};

// Regions generated on demand by modes 5 to 7 and 0.
struct LazyViews {
    std::optional<HeightfieldView> heightfield;
    std::optional<ModelView> eroded;
    std::optional<ModelView> cliffs;
    std::optional<ModelView> sculpted;
};

// Each view of the default world, generated (with a message on stderr) on first use.
const HeightfieldView& heightfieldView(std::optional<HeightfieldView>& view);
// Eroded region with its rivers.
const ModelView& erodedView(std::optional<ModelView>& view);
// Cliff region: cliffs, plateaus and gorges over an eroded region with rivers.
const ModelView& cliffView(std::optional<ModelView>& view);
// Overhangs and arches carved into the cliff region, seen up close.
const ModelView& sculptedView(std::optional<ModelView>& view);

} // namespace viewer
