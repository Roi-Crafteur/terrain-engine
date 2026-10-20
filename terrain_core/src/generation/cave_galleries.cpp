#include "terrain/generation/cave_galleries.hpp"

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace terrain {

namespace {

// The coordinates of noise B are rotated by (0.8, 0.6), as the octaves of fbm2D, and offset by a
// fraction of a cell: its lattice, where every gradient noise is 0, never meets the one of A.
constexpr double kRotationCos = 0.8;
constexpr double kRotationSin = 0.6;
constexpr double kOffsetU = 0.37;
constexpr double kOffsetV = 0.61;

struct GalleryLine {
    double wavelength;
    std::uint64_t seed;
    bool rotated;
};

double lineNoise(const GalleryLine& line, double x, double z) {
    double u = x / line.wavelength;
    double v = z / line.wavelength;
    if (line.rotated) {
        const double rotatedU = kRotationCos * u - kRotationSin * v + kOffsetU;
        v = kRotationSin * u + kRotationCos * v + kOffsetV;
        u = rotatedU;
    }
    return static_cast<double>(noise::gradient2D(u, v, line.seed));
}

// Horizontal distance in meters to the zero line of the noise, to first order: |n| / |grad n|,
// the gradient from central differences, never taken below gradientFloor / wavelength (near
// the saddles of the noise, where it vanishes, the gallery widens into a chamber).
double lineDistance(const GalleryLine& line, double x, double z, const GalleryParams& p) {
    const double h = p.gradientStep;
    const double dx = (lineNoise(line, x + h, z) - lineNoise(line, x - h, z)) / (2.0 * h);
    const double dz = (lineNoise(line, x, z + h) - lineNoise(line, x, z - h)) / (2.0 * h);
    const double slope = std::max(std::hypot(dx, dz), p.gradientFloor / line.wavelength);
    return std::abs(lineNoise(line, x, z)) / slope;
}

// Weight of the far node of a cell, t in [0, 1]: the cubic Hermite curve with zero tangents,
// within [0, 1], so that every interpolated value is a convex combination of the nodes.
double nodeWeight(double t) {
    return t * t * (3.0 - 2.0 * t);
}

std::uint64_t nodeKey(int i, int k) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)) << 32) |
           static_cast<std::uint32_t>(k);
}

} // namespace

double AquiferCache::galleryNode(int i, int k) {
    const auto [it, inserted] = galleryNodes_.try_emplace(nodeKey(i, k));
    if (inserted) {
        const double spacing = field_->params().galleries.levelSpacing;
        double lowest = std::numeric_limits<double>::max();
        for (int n = 0; n < 9; ++n) {
            const double x = (i + 0.5 * (n % 3 - 1)) * spacing;
            const double z = (k + 0.5 * (n / 3 - 1)) * spacing;
            lowest = std::min(lowest, static_cast<double>(ground_->height(x, z)));
        }
        it->second = lowest;
    }
    return it->second;
}

double AquiferCache::galleryLevel(double x, double z) {
    const double spacing = field_->params().galleries.levelSpacing;
    const double u = x / spacing;
    const double v = z / spacing;
    const int i = static_cast<int>(std::floor(u));
    const int k = static_cast<int>(std::floor(v));
    const double wu = nodeWeight(u - i);
    const double wv = nodeWeight(v - k);
    const double low = (1.0 - wu) * galleryNode(i, k) + wu * galleryNode(i + 1, k);
    const double high = (1.0 - wu) * galleryNode(i, k + 1) + wu * galleryNode(i + 1, k + 1);
    return (1.0 - wv) * low + wv * high;
}

std::array<double, 2> CaveField::galleryDistances(double x, double z) const {
    const GalleryParams& p = params_.galleries;
    const GalleryLine a{p.wavelengthA, galleryASeed_, false};
    const GalleryLine b{p.wavelengthB, galleryBSeed_, true};
    return {lineDistance(a, x, z, p), lineDistance(b, x, z, p)};
}

void CaveField::addGallery(CaveColumn& column, AquiferCache& cells) const {
    const GalleryParams& p = params_.galleries;
    const std::array<double, 2> distances = galleryDistances(column.x, column.z);
    column.galleryDistance = static_cast<float>(std::min(distances[0], distances[1]));
    column.galleryFloor =
        static_cast<float>(cells.galleryLevel(column.x, column.z) - p.floorDepth);
}

float CaveField::galleryVoid(const CaveColumn& column, double y) const {
    const float clamp = params_.voidClamp;
    const float presence = column.profile[CaveProfile::kGalleries];
    if (presence <= 0.0f) {
        return clamp;
    }
    const GalleryParams& p = params_.galleries;
    const double halfWidth = static_cast<double>(p.halfWidth * presence);
    const double halfHeight = static_cast<double>(p.halfHeight * presence);
    const double across = static_cast<double>(column.galleryDistance) / halfWidth;
    const double up = (y - (static_cast<double>(column.galleryFloor) + halfHeight)) / halfHeight;
    const auto ellipse =
        static_cast<float>((std::sqrt(across * across + up * up) - 1.0) * halfHeight);
    // Under presenceOn the gallery fades into the rock, up to voidClamp as the field reaches 0:
    // the void is continuous with the one of no gallery.
    const float fade = (1.0f - presence / p.presenceOn) * clamp;
    return std::clamp(std::max(ellipse, fade), -clamp, clamp);
}

} // namespace terrain
