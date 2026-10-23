#include "terrain/streaming/horizon_normals.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace terrain {

namespace {

bool isPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

// Heights of the fine lattice of a level over [x0, x0 + width) x [z0, z0 + height) (fine
// indices, continuous), each at its canonical position, row by row.
std::vector<float> fineHeights(const Heightfield& field, HorizonIndex counts, int spacing,
                               std::int64_t x0, std::int64_t z0, std::int64_t width,
                               std::int64_t height) {
    const auto w = static_cast<std::size_t>(width);
    std::vector<float> heights(w * static_cast<std::size_t>(height));
    std::vector<double> xs(w);
    std::vector<double> zs(w);
    for (std::int64_t i = 0; i < width; ++i) {
        xs[static_cast<std::size_t>(i)] = samplePosition(wrapIndex(x0 + i, counts.x), spacing);
    }
    for (std::int64_t k = 0; k < height; ++k) {
        const double z = samplePosition(wrapIndex(z0 + k, counts.z), spacing);
        for (double& value : zs) {
            value = z;
        }
        field.heightsAt(xs, zs,
                        std::span<float>(heights).subspan(static_cast<std::size_t>(k) * w, w));
    }
    return heights;
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
    const int step = spacing(level);
    const HorizonIndex levelCount = levelCounts(params_, level);
    const HorizonIndex counts{levelCount.x * ratio_, levelCount.z * ratio_};
    const std::int64_t width = rect.width() * ratio_;
    const std::int64_t height = rect.height() * ratio_;
    // One texel of margin around the texels, for the filter.
    const std::int64_t side = width + 2;
    const std::vector<float> h = fineHeights(*field_, counts, step, rect.x0 * ratio_ - 1,
                                             rect.z0 * ratio_ - 1, side, height + 2);
    const auto at = [&](std::int64_t i, std::int64_t k) {
        return static_cast<double>(h[static_cast<std::size_t>(k * side + i)]);
    };
    const double run = 8.0 * step;
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
