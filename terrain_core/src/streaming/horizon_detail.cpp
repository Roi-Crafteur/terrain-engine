#include "terrain/streaming/horizon_detail.hpp"

#include "terrain/streaming/horizon_fine_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace terrain {

namespace {

// The heights of the samples of a rectangle, from the nodes of a grid.
std::vector<float> pointsOf(const HorizonFineGrid& grid, const HorizonRect& rect) {
    std::vector<float> points;
    points.reserve(static_cast<std::size_t>(rect.count()));
    const std::int64_t g = grid.ratio();
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            points.push_back(grid.at(i * g, k * g));
        }
    }
    return points;
}

std::optional<HorizonNormalSampler> normalSampler(const Heightfield& field,
                                                  const HorizonParams& params, int ratio) {
    if (ratio == 0) {
        return std::nullopt;
    }
    return HorizonNormalSampler(field, params, ratio);
}

} // namespace

HorizonDetailSampler::HorizonDetailSampler(const Heightfield& field, const HorizonParams& params,
                                           int normalRatio)
    : field_(&field), normals_(normalSampler(field, params, normalRatio)),
      crests_(field, params) {}

bool HorizonDetailSampler::hasDetail(int level) const {
    return normals_.has_value() || crests_.raises(level);
}

bool HorizonDetailSampler::shared(int level) const {
    return normals_ && crests_.raises(level) && normals_->ratio() % params().crestRatio == 0;
}

HorizonDetail HorizonDetailSampler::sample(int level, const HorizonRect& rect) const {
    if (level < 0 || level >= params().levels) {
        throw std::invalid_argument("HorizonDetailSampler: no such level");
    }
    HorizonDetail detail;
    if (rect.empty()) {
        return detail;
    }
    if (shared(level)) {
        sampleShared(level, rect, detail);
        return detail;
    }
    if (normals_) {
        detail.slopes = normals_->sample(level, rect);
    }
    if (crests_.raises(level)) {
        sampleLifts(level, rect, detail);
    }
    return detail;
}

// One grid at the normal ratio over the cells of the crests: the nodes of the normals first, in
// dense rows, then those the crests add.
void HorizonDetailSampler::sampleShared(int level, const HorizonRect& rect,
                                        HorizonDetail& detail) const {
    const int ratio = normals_->ratio();
    const HorizonRect cells{(rect.x0 - 1) * ratio, (rect.z0 - 1) * ratio, rect.x1 * ratio + 1,
                            rect.z1 * ratio + 1};
    HorizonFineGrid grid(params(), level, ratio, cells);
    const HorizonRect dense = normals_->nodes(rect);
    grid.read(*field_, dense);
    crests_.read(level, rect, grid, dense);
    detail.slopes.resize(static_cast<std::size_t>(normals_->texelCount(rect)));
    normals_->fill(level, rect, grid, detail.slopes);
    detail.lifts.resize(static_cast<std::size_t>(rect.count()));
    crests_.fill(level, rect, grid, detail.lifts);
    detail.points = pointsOf(grid, rect);
}

// The lifts alone, on a grid at the crest ratio.
void HorizonDetailSampler::sampleLifts(int level, const HorizonRect& rect,
                                       HorizonDetail& detail) const {
    HorizonFineGrid grid(params(), level, params().crestRatio, crests_.nodes(level, rect));
    crests_.read(level, rect, grid);
    detail.lifts.resize(static_cast<std::size_t>(rect.count()));
    crests_.fill(level, rect, grid, detail.lifts);
    detail.points = pointsOf(grid, rect);
}

} // namespace terrain
