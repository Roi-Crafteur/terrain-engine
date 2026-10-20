#include "terrain/generation/cave_galleries.hpp"

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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

namespace {

// Rock of the band of a river, meters, positive inside: within shell of the half width of the
// gallery horizontally, from shell under its floor up to freeboard over the deepest water.
double bandGeometry(const CaveColumn& column, double y, const CaveParams& params) {
    const CaveRiverParams& r = params.rivers;
    const double halfWidth = static_cast<double>(params.galleries.halfWidth *
                                                 column.profile[CaveProfile::kGalleries]);
    const double floor = static_cast<double>(column.galleryFloor);
    const double across = halfWidth + r.shell - static_cast<double>(column.galleryDistance);
    const double above = y - (floor - r.shell);
    const double below = floor + r.maxDepth + r.freeboard - y;
    return std::min({across, above, below});
}

// The rock of a band where the river field closes it (strength in [0, 1]): from -voidClamp at
// strength 0 up to the rock itself at 1, continuously.
float closedBy(double rock, float strength, float clamp) {
    const auto bounded = static_cast<float>(std::clamp(rock, -static_cast<double>(clamp),
                                                       static_cast<double>(clamp)));
    return -clamp + strength * (bounded + clamp);
}

float bankStrength(const CaveColumn& column, const CaveRiverParams& r) {
    return noise::smoothstep(r.bankLow, r.bankHigh, column.profile[CaveProfile::kRiver]);
}

} // namespace

float CaveField::bankVoid(const CaveColumn& column, double y) const {
    const float strength = bankStrength(column, params_.rivers);
    if (strength <= 0.0f) {
        return -params_.voidClamp;
    }
    return closedBy(bandGeometry(column, y, params_), strength, params_.voidClamp);
}

// Where the field crosses the threshold, the water begins: the sill closes the bottom of the
// gallery around it, sillWidth of the field on each side, so that the water ends against rock.
float CaveField::sillVoid(const CaveColumn& column, double y) const {
    const CaveRiverParams& r = params_.rivers;
    const float strength = bankStrength(column, r);
    if (strength <= 0.0f) {
        return -params_.voidClamp;
    }
    const float offset = std::abs(column.profile[CaveProfile::kRiver] - r.threshold);
    const double across = static_cast<double>((r.sillWidth - offset) / r.sillWidth) *
                          static_cast<double>(params_.voidClamp);
    return closedBy(std::min(bandGeometry(column, y, params_), across), strength,
                    params_.voidClamp);
}

float CaveField::riverBandTop(const CaveColumn& column) const {
    const CaveRiverParams& r = params_.rivers;
    if (bankStrength(column, r) <= 0.0f) {
        return kDryLevel;
    }
    return column.galleryFloor + r.maxDepth + r.freeboard;
}

// The band top of the columns at (x, z) and at 8 points reach away around it, where the axis of
// a gallery may lie within the band: its distance grows by 1.5 m per meter at most.
float AquiferCache::riverBandTopNear(double x, double z, double reach) {
    const CaveDistricts* districts = field_->districts();
    if (districts == nullptr) {
        return kDryLevel;
    }
    const CaveParams& params = field_->params();
    // The districts weighing within reach of (x, z) are among the 5 x 5 cells around it: no
    // band where none of them has rivers.
    const CaveDistrictParams& d = districts->params();
    const int baseX = static_cast<int>(std::floor(x / d.cellSize));
    const int baseZ = static_cast<int>(std::floor(z / d.cellSize));
    bool rivers = false;
    for (int n = 0; n < 25 && !rivers; ++n) {
        const CaveDistrict& around = district(baseX + n % 5 - 2, baseZ + n / 5 - 2);
        rivers = d.profiles[static_cast<std::size_t>(around.type)][CaveProfile::kRiver] > 0.0f;
    }
    if (!rivers) {
        return kDryLevel;
    }
    float highest = kDryLevel;
    for (int n = 0; n < 9; ++n) {
        const double angle = static_cast<double>(n) * std::numbers::pi / 4.0;
        const double px = n == 8 ? x : x + reach * std::cos(angle);
        const double pz = n == 8 ? z : z + reach * std::sin(angle);
        const CaveColumn column = field_->column(px, pz, ground_->height(px, pz), *ground_, {},
                                                 this, false);
        const float top = field_->riverBandTop(column);
        const double halfWidth = static_cast<double>(params.galleries.halfWidth *
                                                     column.profile[CaveProfile::kGalleries]);
        const double band = halfWidth + params.rivers.shell + 1.5 * reach;
        if (top > kDryLevel && static_cast<double>(column.galleryDistance) < band) {
            highest = std::max(highest, top);
        }
    }
    return highest;
}

} // namespace terrain
