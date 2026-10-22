#include "horizon_normal_texels.hpp"

#include "horizon_textures.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <stdexcept>

namespace terrain::unreal {

std::uint16_t halfFromFloat(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000U);
    const std::uint32_t exponent = (bits >> 23) & 0xFFU;
    std::uint32_t mantissa = bits & 0x7FFFFFU;
    if (exponent == 0xFFU) { // infinity or NaN
        return static_cast<std::uint16_t>(sign | 0x7C00U | (mantissa != 0 ? 0x200U : 0U));
    }
    const int e = static_cast<int>(exponent) - 127 + 15;
    if (e >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00U); // too large: infinity
    }
    if (e <= 0) { // subnormal half, or zero
        if (e < -10) {
            return sign;
        }
        mantissa |= 0x800000U;
        const auto shift = static_cast<std::uint32_t>(14 - e);
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t rest = mantissa & ((1U << shift) - 1U);
        const std::uint32_t middle = 1U << (shift - 1U);
        if (rest > middle || (rest == middle && (half & 1U) != 0)) {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = (static_cast<std::uint32_t>(e) << 10) | (mantissa >> 13);
    const std::uint32_t rest = mantissa & 0x1FFFU;
    if (rest > 0x1000U || (rest == 0x1000U && (half & 1U) != 0)) {
        ++half; // may carry into the exponent, up to infinity: still the nearest
    }
    return static_cast<std::uint16_t>(sign | half);
}

float floatFromHalf(std::uint16_t half) {
    const std::uint32_t sign = (static_cast<std::uint32_t>(half) & 0x8000U) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1FU;
    std::uint32_t mantissa = half & 0x3FFU;
    if (exponent == 0x1FU) {
        return std::bit_cast<float>(sign | 0x7F800000U | (mantissa << 13));
    }
    if (exponent == 0) {
        if (mantissa == 0) {
            return std::bit_cast<float>(sign);
        }
        int e = -1;
        do { // normalize the subnormal
            ++e;
            mantissa <<= 1;
        } while ((mantissa & 0x400U) == 0);
        const auto bits = sign | (static_cast<std::uint32_t>(127 - 15 - e) << 23) |
                          ((mantissa & 0x3FFU) << 13);
        return std::bit_cast<float>(bits);
    }
    return std::bit_cast<float>(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));
}

std::vector<std::uint16_t> normalTexels(std::span<const DetailSlope> slopes) {
    std::vector<std::uint16_t> data;
    data.reserve(slopes.size() * kNormalHalves);
    for (const DetailSlope& slope : slopes) {
        data.push_back(halfFromFloat(std::clamp(slope.gradX, -kNormalSlopeLimit, kNormalSlopeLimit)));
        data.push_back(halfFromFloat(std::clamp(slope.gradZ, -kNormalSlopeLimit, kNormalSlopeLimit)));
    }
    return data;
}

std::vector<NormalRegion> normalRegions(int level, int side, int ratio, const HorizonRect& rect,
                                        std::span<const std::uint16_t> data) {
    const std::int64_t width = rect.width() * ratio;
    if (rect.width() > side || rect.height() > side ||
        data.size() != static_cast<std::size_t>(rect.count() * ratio * ratio) * kNormalHalves) {
        throw std::invalid_argument("normalRegions: the texels do not fit the rectangle");
    }
    std::vector<NormalRegion> regions;
    for (const SlotSpan& z : slotSpans(rect.z0, rect.z1, side)) {
        for (const SlotSpan& x : slotSpans(rect.x0, rect.x1, side)) {
            NormalRegion region{level, x.slot * ratio, z.slot * ratio, x.length * ratio,
                                z.length * ratio, {}};
            const auto rowHalves = static_cast<std::ptrdiff_t>(region.width) * kNormalHalves;
            region.data.reserve(static_cast<std::size_t>(rowHalves * region.height));
            for (int row = 0; row < region.height; ++row) {
                const std::int64_t texelRow = (z.sample - rect.z0) * ratio + row;
                const std::int64_t first =
                    (texelRow * width + (x.sample - rect.x0) * ratio) * kNormalHalves;
                const auto begin = data.begin() + static_cast<std::ptrdiff_t>(first);
                region.data.insert(region.data.end(), begin, begin + rowHalves);
            }
            regions.push_back(std::move(region));
        }
    }
    return regions;
}

std::vector<NormalRegion> pendingRegions(int level, int side, int ratio, const HorizonRect& rect) {
    const std::vector<std::uint16_t> pending(
        static_cast<std::size_t>(rect.count() * ratio * ratio) * kNormalHalves, kNormalPendingHalf);
    return normalRegions(level, side, ratio, rect, pending);
}

} // namespace terrain::unreal
