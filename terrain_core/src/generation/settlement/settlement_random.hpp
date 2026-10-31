#pragma once

// Internal to the settlement: the draws of the seed. Each draw has its own stream, the FNV-1a of
// its name with the top bit set (at least 2^63), so that it never meets the layers of the relief
// (small numbers) and a new draw never changes the old ones: tirage(seed + "kingdoms", min, max)
// of docs/PEUPLEMENT.md is drawCount(seed, "kingdoms", ...).

#include "terrain/generation/noise.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/json/json.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace terrain::settlement {

[[nodiscard]] inline std::uint64_t streamOf(std::uint64_t seed, std::string_view name) {
    return noise::deriveSeed(seed, json::fnv1a64(name) | (std::uint64_t{1} << 63));
}

// A draw in [0, 1) of a stream, index by index.
[[nodiscard]] inline double unitDraw(std::uint64_t stream, std::uint64_t index) {
    return static_cast<double>(noise::deriveSeed(stream, index) >> 11) * 0x1.0p-53;
}

// A whole number in [low, high], each with the same chance.
[[nodiscard]] inline int drawBetween(std::uint64_t stream, std::uint64_t index, int low,
                                     int high) {
    const auto span = static_cast<std::uint64_t>(high - low) + 1u;
    return low + static_cast<int>(noise::deriveSeed(stream, index) % span);
}

[[nodiscard]] inline double drawIn(std::uint64_t stream, std::uint64_t index, const RealRange& r) {
    return r.min + (r.max - r.min) * unitDraw(stream, index);
}

// A count of the reference planet drawn in its range, scaled to a world of scale times its area,
// rounded to the nearest whole number.
[[nodiscard]] inline int drawCount(std::uint64_t seed, std::string_view name,
                                   const CountRange& range, double scale) {
    const int drawn = drawBetween(streamOf(seed, name), 0, range.min, range.max);
    return static_cast<int>(std::floor(drawn * scale + 0.5));
}

} // namespace terrain::settlement
