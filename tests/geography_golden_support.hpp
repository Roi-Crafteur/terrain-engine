#pragma once

// Golden checksums of the planetary geography (phase 11R): the whole precomputed map (every
// field of every node and its anchor) of the default world and of a small world, and the heights
// of the relief it gives at the scales of a continent, a region and a walk along the coast at the
// origin. Asserted on every platform and under FTZ/DAZ (golden_ftz_test).

#include "terrain/generation/heightfield.hpp"

#include "golden_support.hpp"

#include <cstdint>
#include <vector>

namespace golden {

namespace geography_detail {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

inline terrain::HeightfieldParams geographyParams() {
    terrain::HeightfieldParams params;
    params.geography.enabled = true;
    return params;
}

// 2,097 x 1,049 km.
inline const terrain::WorldSize kSmallWorld{32.0 * 65536.0, 16.0 * 65536.0};

// Every field of side x side samples, step meters apart, from (x0, z0).
inline std::uint64_t samplesChecksum(const terrain::Heightfield& field, double x0, double z0,
                                     double step, int side) {
    Checksum sum;
    for (int k = 0; k < side; ++k) {
        for (int i = 0; i < side; ++i) {
            const terrain::HeightSample s = field.sampleAt(x0 + i * step, z0 + k * step);
            for (const float v : {s.continentalness, s.macroContinentalness, s.plateContinentalness,
                                  s.plateCollision, s.mountainMask, s.base, s.mountain, s.detail,
                                  s.height}) {
                sum.add(v);
            }
        }
    }
    return sum.value();
}

} // namespace geography_detail

inline std::vector<Part> geographyParts() {
    using namespace geography_detail;
    const terrain::Heightfield field(kSeed, geographyParams());
    const terrain::Heightfield small(kSeed, geographyParams(), kSmallWorld);
    return {
        {"geography map", field.geography()->checksum(), 0xDE0CB2FDDE6A3587ULL},
        {"geography map, small world", small.geography()->checksum(), 0xB211DC1FB6AFA7A0ULL},
        {"geography 1000 km", samplesChecksum(field, -16e6, -16e6, 1e6, 32), 0x40C57FBEB1B215CAULL},
        {"geography 10 km", samplesChecksum(field, -320e3, -320e3, 10e3, 64),
         0x269E258C47B1AAD3ULL},
        {"geography coast 25 m", samplesChecksum(field, -800.0, -800.0, 25.0, 64),
         0x63ED5D7872072ECBULL},
    };
}

} // namespace golden
