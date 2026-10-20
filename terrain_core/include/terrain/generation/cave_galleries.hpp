#pragma once

#include <cmath>

namespace terrain {

// Galleries: the large connected network of the caves (CaveProfile::kGalleries), wide tunnels
// along the zero lines of two 2D gradient noises, whose crossings are the junctions of the
// network. Every gallery runs on one smooth level shared by all of them, so that they meet at
// the same height where they cross: their floor lies floorDepth under the lowest ground around,
// smoothed over a world lattice of levelSpacing meters.
struct GalleryParams {
    // Paths: the zero lines of noises A and B, B rotated and offset so that their crossings
    // never fall on a grid. The distance to a line is |n| / max(|grad n|, gradientFloor / L),
    // the gradient from central differences gradientStep meters apart: real meters.
    double wavelengthA = 704.0;
    double wavelengthB = 576.0;
    double gradientFloor = 0.25;
    double gradientStep = 0.5;
    // Elliptic section of a gallery of the full profile (kGalleries = 1), scaled by the field;
    // the floor stays where it is.
    float halfWidth = 5.0f;
    float halfHeight = 3.5f;
    // Below this field the gallery fades out into the rock, reaching nothing at 0.
    float presenceOn = 0.25f;
    // Level: a node every levelSpacing meters is the lowest ground of 3 x 3 probes half a
    // spacing apart around it; the floor is floorDepth under the nodes, interpolated.
    double levelSpacing = 128.0;
    float floorDepth = 24.0f;
};

// Underground rivers (CaveProfile::kRiver): water in the galleries where the river field
// reaches threshold, maxDepth deep where the smoothed floor is flat, thinning out to nothing
// where it gets steep: its surface never slopes more than maxSlope. Around a river the other
// caves are closed under the band (shell meters of rock around the gallery, from shell under
// its floor to freeboard over the deepest water), and a sill closes the bottom of the gallery
// where the field crosses the threshold: the water only lies in the gallery, enclosed by rock.
struct CaveRiverParams {
    float maxSlope = 0.05f;
    float maxDepth = 1.0f;
    float threshold = 0.65f;
    float sillWidth = 0.05f; // of the river field around the threshold
    float shell = 3.0f;
    float freeboard = 0.5f;
    float bankLow = 0.3f; // the band closes the other caves from bankLow to bankHigh
    float bankHigh = 0.5f;
};

// Flatness thresholds of the nodes of the level derived from maxSlope: the slope of the floor
// stays under 1.5 sqrt(2) high where the water lies (smoothstep weights rise by 1.5 per cell at
// most, on both axes), and its depth adds at most 1.5 sqrt(2) maxDepth / (3 spacing) (the
// flatness averaged over 3 x 3 nodes changes by 1/3 at most from a node to the next).
struct CaveRiverFlatness {
    double low = 0.0;
    double high = 0.0;
};

[[nodiscard]] inline CaveRiverFlatness riverFlatness(const CaveRiverParams& river,
                                                     const GalleryParams& galleries) {
    const double rise = 1.5 * std::sqrt(2.0);
    const double high = river.maxSlope / rise - river.maxDepth / (3.0 * galleries.levelSpacing);
    return {high / 2.0, high};
}

// Largest slope of the surface of a river, from the flatness above: at most maxSlope.
[[nodiscard]] inline double riverSlopeBound(const CaveRiverParams& river,
                                            const GalleryParams& galleries) {
    const double rise = 1.5 * std::sqrt(2.0);
    return rise * (riverFlatness(river, galleries).high +
                   river.maxDepth / (3.0 * galleries.levelSpacing));
}

} // namespace terrain
