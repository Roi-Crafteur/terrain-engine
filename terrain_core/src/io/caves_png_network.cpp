#include "caves_png_layers.hpp"

#include <algorithm>
#include <cmath>

// The network of galleries, the underground rivers and the lava on the map of the caves. The
// vertical scan of the map (4 m apart) cannot see them: a gallery is 7 m high at most, a river
// 1 m deep, and the axis of a gallery is rarely under the center of a sample (20 m apart). They
// are found from the column of the sample instead, and the density only confirms them.

namespace terrain::cave_png {

namespace {

// A sample is crossed by a gallery whose axis lies within its half width (CaveColumn::
// galleryDistance), or within this many pixels if that is smaller: the network stays a line
// of connected samples however wide the map's pixels are.
constexpr double kMinGalleryReach = 0.75;
// A river thinner than its minimum depth plus this (meters) is a film at most: not drawn.
constexpr double kRiverMargin = 0.1;
// The lava is searched from just under its level down to this far under it, every kLavaStep.
constexpr double kLavaReach = 10.0;
constexpr double kLavaStep = 2.0;
// Share of the color of the river and of the lava in the color of a sample: the whole of it.
constexpr float kFluidShare = 1.0f;

// Depth of the river of a column at its gallery, meters: its level over the floor, plus the
// minimum depth the level lies under it. 0 without a river.
double riverDepth(const CaveField& field, const CaveColumn& column) {
    if (column.riverLevel <= kDryLevel) {
        return 0.0;
    }
    return static_cast<double>(column.riverLevel) - static_cast<double>(column.galleryFloor) +
           static_cast<double>(field.params().rivers.minDepth);
}

// The column moved onto the axis of its gallery.
CaveColumn onAxis(const CaveColumn& column) {
    CaveColumn axis = column;
    axis.galleryDistance = 0.0f;
    return axis;
}

// Whether the axis of a gallery crosses the sample: close enough, a gallery of the profile of
// the column (its void is negative on the axis), and caves allowed at the middle of its height.
bool galleryCrosses(const CaveField& field, const CaveColumn& column, double metersPerPixel) {
    const GalleryParams& galleries = field.params().galleries;
    const double presence = static_cast<double>(column.profile[CaveProfile::kGalleries]);
    const double reach = std::max(static_cast<double>(galleries.halfWidth) * presence,
                                  kMinGalleryReach * metersPerPixel);
    if (presence <= 0.0 || static_cast<double>(column.galleryDistance) > reach) {
        return false;
    }
    const double halfHeight = static_cast<double>(galleries.halfHeight) * presence;
    const double middle = static_cast<double>(column.galleryFloor) + halfHeight;
    return field.galleryVoid(onAxis(column), middle) < 0.0f && field.limit(column, middle) < 0.0f;
}

// Whether the river of the gallery that crosses the sample holds water at mid-depth on its
// axis: the carved density there is air and the water density says water.
bool riverHoldsWater(CaveSampler& sampler, const CaveColumn& column) {
    const CaveField& field = sampler.field();
    const double y = static_cast<double>(column.galleryFloor) + riverDepth(field, column) / 2.0;
    if (field.limit(column, y) >= 0.0f) {
        return false;
    }
    const CaveColumn axis = onAxis(column);
    const auto terrain = static_cast<float>(y - static_cast<double>(column.ground));
    const float carved = sampler.density(axis, y, terrain);
    return carved >= 0.0f && sampler.waterDensity(axis, y, terrain, carved) < 0.0f;
}

// Whether carved air holds lava within kLavaReach under the lava level of the column: samples
// from kLavaStep / 2 under it down, over the depths the scan of the map covers.
bool lavaUnderLevel(CaveSampler& sampler, const CaveColumn& column) {
    const CaveParams& params = sampler.field().params();
    for (double under = kLavaStep / 2.0; under <= kLavaReach; under += kLavaStep) {
        const double y = static_cast<double>(column.lavaLevel) - under;
        const double depth = static_cast<double>(column.ground) - y;
        if (depth < params.minRoof - 1.0 || depth > params.maxDepth + 1.0) {
            continue;
        }
        const auto terrain = static_cast<float>(-depth);
        const float carved = sampler.density(column, y, terrain);
        if (carved >= 0.0f && sampler.lavaDensity(column, y, terrain, carved) < 0.0f) {
            return true;
        }
    }
    return false;
}

void paint(Image& image, std::size_t sample, Rgb rgb, float share) {
    for (std::size_t c = 0; c < 3; ++c) {
        image.pixels[sample * 3 + c] = mix(image.pixels[sample * 3 + c], rgb[c], share);
    }
}

} // namespace

void scanNetwork(CaveSampler& sampler, const CaveColumn& column, double metersPerPixel,
                 Scan& scan) {
    const CaveField& field = sampler.field();
    if (column.lavaLevel > kDryLevel) {
        scan.lava = lavaUnderLevel(sampler, column);
    }
    if (!galleryCrosses(field, column, metersPerPixel)) {
        return;
    }
    scan.gallery = true;
    if (riverDepth(field, column) > static_cast<double>(field.params().rivers.minDepth) +
                                        kRiverMargin) {
        scan.river = riverHoldsWater(sampler, column);
        scan.riverFrozen = scan.river && column.cold;
    }
}

void drawNetwork(Image& image, const std::vector<Scan>& scans) {
    for (std::size_t i = 0; i < scans.size(); ++i) {
        if (scans[i].gallery) {
            paint(image, i, kCaveGalleryRgb, kCaveGalleryShare);
        }
        if (scans[i].river) {
            paint(image, i, scans[i].riverFrozen ? kCaveIceRgb : kCaveRiverWaterRgb, kFluidShare);
        }
        if (scans[i].lava) {
            paint(image, i, kCaveLavaRgb, kFluidShare);
        }
    }
}

} // namespace terrain::cave_png
