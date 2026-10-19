#include "model_views.hpp"

#include "cliff_region_view.hpp"
#include "eroded_region_view.hpp"
#include "heightfield_region.hpp"
#include "sculpted_region_view.hpp"
#include "terrain/generation/heightfield.hpp"

#include <cstdio>

namespace viewer {

HeightfieldView::HeightfieldView(const HeightfieldRegion& region)
    : mesh(region.mesh), altitudes{region.minHeight, region.maxHeight},
      viewpoint(region.viewpoint) {}

const HeightfieldView& heightfieldView(std::optional<HeightfieldView>& view) {
    if (!view) {
        std::fprintf(stderr, "Generating the heightfield region (seed %llu)...\n",
                     static_cast<unsigned long long>(terrain::kDefaultWorldSeed));
        const terrain::Heightfield field(terrain::kDefaultWorldSeed);
        view.emplace(buildHeightfieldRegion(field));
    }
    return *view;
}

const ModelView& erodedView(std::optional<ModelView>& view) {
    if (!view) {
        std::fprintf(stderr, "Eroding a region and tracing its rivers (seed %llu)...\n",
                     static_cast<unsigned long long>(terrain::kDefaultWorldSeed));
        const ErodedRegionView region = buildErodedRegionView(terrain::kDefaultWorldSeed);
        std::fprintf(stderr, "%zu river cells, %zu cascades\n", region.riverCells,
                     region.cascades);
        view.emplace(region);
    }
    return *view;
}

const ModelView& cliffView(std::optional<ModelView>& view) {
    if (!view) {
        std::fprintf(stderr, "Carving cliffs, plateaus and gorges (seed %llu)...\n",
                     static_cast<unsigned long long>(terrain::kDefaultWorldSeed));
        const CliffRegionView region = buildCliffRegionView(terrain::kDefaultWorldSeed);
        std::fprintf(stderr, "%zu river cells, %zu cascades, %zu cirques\n", region.riverCells,
                     region.cascades, region.cirques);
        view.emplace(region);
    }
    return *view;
}

const ModelView& sculptedView(std::optional<ModelView>& view) {
    if (!view) {
        std::fprintf(stderr, "Carving overhangs and arches into the cliffs (seed %llu)...\n",
                     static_cast<unsigned long long>(terrain::kDefaultWorldSeed));
        const SculptedRegionView region = buildSculptedRegionView(terrain::kDefaultWorldSeed);
        std::fprintf(stderr,
                     "%zu arches in the region; window of %.0f m: %zu arches, %zu samples with "
                     "rock above air\n",
                     region.arches, kSculptedWindowMeters, region.windowArches,
                     region.overhangSamples);
        view.emplace(region);
    }
    return *view;
}

} // namespace viewer
