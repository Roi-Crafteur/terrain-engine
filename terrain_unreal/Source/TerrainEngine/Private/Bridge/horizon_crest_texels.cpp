#include "horizon_crest_texels.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace terrain::unreal {

std::optional<float> crestHeight(const HorizonCell& cell, float point, float raised) {
    if ((cell.flags & kHorizonEdited) != 0 || cell.height != point || raised == point) {
        return std::nullopt;
    }
    return raised;
}

std::vector<TexelRegion> crestRegions(const HorizonLevel& level, int side, const CrestBand& band,
                                      bool sea) {
    if (static_cast<std::int64_t>(band.points.size()) != band.rect.count() ||
        band.raised.size() != band.points.size()) {
        throw std::invalid_argument("crestRegions: the heights do not fit the band");
    }
    // The rows from the first to the last sample that rises.
    std::int64_t first = band.rect.z1;
    std::int64_t last = band.rect.z0 - 1;
    std::size_t n = 0;
    for (std::int64_t k = band.rect.z0; k < band.rect.z1; ++k) {
        for (std::int64_t i = band.rect.x0; i < band.rect.x1; ++i, ++n) {
            if (crestHeight(level.at({i, k}), band.points[n], band.raised[n])) {
                first = std::min(first, k);
                last = std::max(last, k);
            }
        }
    }
    if (last < first) {
        return {};
    }
    const HorizonRect rows{band.rect.x0, first, band.rect.x1, last + 1};
    std::vector<float> data;
    data.reserve(static_cast<std::size_t>(rows.count()) * kTexelFloats);
    n = static_cast<std::size_t>((first - band.rect.z0) * band.rect.width());
    for (std::int64_t k = rows.z0; k < rows.z1; ++k) {
        for (std::int64_t i = rows.x0; i < rows.x1; ++i, ++n) {
            HorizonCell cell = level.at({i, k});
            if (const std::optional<float> height =
                    crestHeight(cell, band.points[n], band.raised[n])) {
                cell.height = *height;
            }
            const HorizonTexel texel = horizonTexel(cell, sea);
            data.insert(data.end(), {texel.height, texel.gradX, texel.gradZ, texel.color});
        }
    }
    return texelRegions(level.level(), side, rows, data);
}

std::vector<float> partValues(const HorizonRect& rect, std::span<const float> values,
                              const HorizonRect& part) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(part.count()));
    for (std::int64_t k = part.z0; k < part.z1; ++k) {
        const auto begin = values.begin() +
                           static_cast<std::ptrdiff_t>((k - rect.z0) * rect.width() +
                                                       (part.x0 - rect.x0));
        out.insert(out.end(), begin, begin + static_cast<std::ptrdiff_t>(part.width()));
    }
    return out;
}

} // namespace terrain::unreal
