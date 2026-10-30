#pragma once

// The zone of the coastal relief (agent 33): the band of the coast signal of the geography where
// the coasts are shaped, from kBandSea meters of signal out at sea to kBandLand meters inland
// (GeographyParams::coastRamp, over which the mountains fade out toward the shore today). The
// geography itself never changes: a point lies in the zone or out of it before and after the
// coastal relief alike, so the places of the tests are the same on every commit.

#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace coast_zone {

inline constexpr double kBandLand = 30'000.0;
inline constexpr double kBandSea = 20'000.0;

// The coast signal of the geography of field at (x, z), any image of the point.
inline double coastSignal(const terrain::Heightfield& field, double x, double z) {
    const terrain::WorldSize& w = field.world();
    return field.geography()
        ->sampleAt(terrain::wrapCoordinate(x, w.x), terrain::wrapCoordinate(z, w.z))
        .coast;
}

[[nodiscard]] inline bool inZone(double coast) {
    return coast > -kBandSea && coast < kBandLand;
}

// Whether every point of the square [x0, x0 + side]^2, read every step meters (its corners
// included), lies out of the zone by margin meters of signal at least, all on land or all at
// sea.
inline bool squareOutside(const terrain::Heightfield& field, double x0, double z0, double side,
                          double step, double margin) {
    const auto count = static_cast<int>(std::ceil(side / step));
    int land = 0;
    int sea = 0;
    for (int j = 0; j <= count; ++j) {
        for (int i = 0; i <= count; ++i) {
            const double x = x0 + std::min(i * step, side);
            const double z = z0 + std::min(j * step, side);
            const double c = coastSignal(field, x, z);
            land += c >= kBandLand + margin ? 1 : 0;
            sea += c <= -kBandSea - margin ? 1 : 0;
        }
    }
    const int all = (count + 1) * (count + 1);
    return land == all || sea == all;
}

// A deterministic stream of numbers in [0, 1) (xorshift64).
class Draws {
public:
    explicit Draws(std::uint64_t seed) : state_(seed) {}
    double next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<double>(state_ >> 11) * (1.0 / 9007199254740992.0);
    }

private:
    std::uint64_t state_;
};

} // namespace coast_zone
