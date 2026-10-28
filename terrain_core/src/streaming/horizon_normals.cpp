#include "terrain/streaming/horizon_normals.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace terrain {

namespace {

bool isPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

} // namespace

HorizonNormalSampler::HorizonNormalSampler(const Heightfield& field, const HorizonParams& params,
                                           int ratio)
    : field_(&field), params_(params), ratio_(ratio) {
    validate(params_);
    if (!(field.world() == params_.world)) {
        throw std::invalid_argument("HorizonNormalSampler: the relief and the horizon must share "
                                    "one world");
    }
    if (!isPowerOfTwo(ratio) || ratio > kHorizonNormalRatioMax || params_.basePitch % ratio != 0) {
        throw std::invalid_argument("HorizonNormalSampler: the ratio must be a power of two of at "
                                    "most 16 dividing the pitch of level 0");
    }
}

int HorizonNormalSampler::spacing(int level) const {
    return levelPitch(params_, level) / ratio_;
}

std::int64_t HorizonNormalSampler::texelCount(const HorizonRect& rect) const {
    return rect.count() * ratio_ * ratio_;
}

HorizonRect HorizonNormalSampler::nodes(const HorizonRect& rect) const {
    if (rect.empty()) {
        return {};
    }
    return {rect.x0 * ratio_ - 1, rect.z0 * ratio_ - 1, rect.x1 * ratio_ + 1, rect.z1 * ratio_ + 1};
}

void HorizonNormalSampler::fill(int level, const HorizonRect& rect,
                                std::span<DetailSlope> out) const {
    if (level < 0 || level >= params_.levels) {
        throw std::invalid_argument("HorizonNormalSampler: no such level");
    }
    if (static_cast<std::int64_t>(out.size()) != texelCount(rect)) {
        throw std::invalid_argument("HorizonNormalSampler: the output does not fit the rectangle");
    }
    if (rect.empty()) {
        return;
    }
    HorizonFineGrid grid(params_, level, ratio_, nodes(rect));
    grid.read(*field_, grid.box());
    fill(level, rect, grid, out);
}

void HorizonNormalSampler::fill(int level, const HorizonRect& rect, const HorizonFineGrid& grid,
                                std::span<DetailSlope> out) const {
    if (level < 0 || level >= params_.levels) {
        throw std::invalid_argument("HorizonNormalSampler: no such level");
    }
    if (static_cast<std::int64_t>(out.size()) != texelCount(rect)) {
        throw std::invalid_argument("HorizonNormalSampler: the output does not fit the rectangle");
    }
    const int stride = grid.ratio() / ratio_;
    const HorizonRect needed = nodes(rect);
    if (grid.level() != level || grid.ratio() % ratio_ != 0 ||
        !grid.covers({needed.x0 * stride, needed.z0 * stride, (needed.x1 - 1) * stride + 1,
                      (needed.z1 - 1) * stride + 1})) {
        throw std::invalid_argument("HorizonNormalSampler: the grid does not hold the texels");
    }
    if (rect.empty()) {
        return;
    }
    const std::int64_t width = rect.width() * ratio_;
    const std::int64_t height = rect.height() * ratio_;
    // Node (i, k) of the texels and of their margin of one, from the low corner of the margin.
    const auto at = [&](std::int64_t i, std::int64_t k) {
        return static_cast<double>(grid.at((needed.x0 + i) * stride, (needed.z0 + k) * stride));
    };
    const double run = 8.0 * spacing(level);
    std::size_t next = 0;
    for (std::int64_t k = 1; k <= height; ++k) {
        for (std::int64_t i = 1; i <= width; ++i) {
            const double east = at(i + 1, k - 1) + 2.0 * at(i + 1, k) + at(i + 1, k + 1);
            const double west = at(i - 1, k - 1) + 2.0 * at(i - 1, k) + at(i - 1, k + 1);
            const double south = at(i - 1, k + 1) + 2.0 * at(i, k + 1) + at(i + 1, k + 1);
            const double north = at(i - 1, k - 1) + 2.0 * at(i, k - 1) + at(i + 1, k - 1);
            out[next++] = {static_cast<float>((east - west) / run),
                           static_cast<float>((south - north) / run)};
        }
    }
}

std::vector<DetailSlope> HorizonNormalSampler::sample(int level, const HorizonRect& rect) const {
    std::vector<DetailSlope> slopes(static_cast<std::size_t>(texelCount(rect)));
    fill(level, rect, slopes);
    return slopes;
}

} // namespace terrain
