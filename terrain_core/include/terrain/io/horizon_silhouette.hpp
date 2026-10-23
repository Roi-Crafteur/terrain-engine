#pragma once

#include "terrain/io/horizon_panorama.hpp"
#include "terrain/io/png.hpp"

#include <limits>
#include <vector>

namespace terrain {

// Comparisons of the silhouettes of the far horizon (traceSilhouette) with a true silhouette, for
// terrain_export_silhouette: elevation angles in degrees, the curvature of the planet included.

// Elevation angle of a column of a silhouette, degrees: -90 where no ground was found.
[[nodiscard]] double silhouetteDegrees(double tangent);

// Angles of a silhouette minus those of the truth, over the columns where the true silhouette
// lies within [minDistance, maxDistance) meters of the eye and both found ground.
struct SilhouetteError {
    int columns = 0;
    double meanDegrees = 0.0; // negative when the silhouette is too low
    double meanAbsDegrees = 0.0;
    double p95AbsDegrees = 0.0; // 95th percentile of the absolute differences
    double maxAbsDegrees = 0.0;
};

// Throws std::invalid_argument for silhouettes of different sizes.
[[nodiscard]] SilhouetteError
silhouetteError(const Silhouette& silhouette, const Silhouette& truth, double minDistance = 0.0,
                double maxDistance = std::numeric_limits<double>::infinity());

// Silhouettes of the same columns (the truth first), one band of bandHeight rows each from the
// top: the sky over each silhouette, its ground colored by the distance of the silhouette (warm
// near, cool far, on a log scale from 1 to 500 km), the line of the truth in red over the
// others. Rows are linear in the elevation angle over the range of the truth, one degree of
// margin above and below. Then a band of the differences from the truth, rows linear from
// +errorRangeDegrees (top) to -errorRangeDegrees (clamped), a gray line every quarter of the
// range: orange for the second silhouette, blue for the third, green for the fourth. Throws
// std::invalid_argument for fewer than one silhouette, silhouettes of different sizes or no
// column, or a band of fewer than 8 rows.
[[nodiscard]] Image renderSilhouettes(const std::vector<Silhouette>& silhouettes,
                                      int bandHeight = 300, double errorRangeDegrees = 1.0);

} // namespace terrain
