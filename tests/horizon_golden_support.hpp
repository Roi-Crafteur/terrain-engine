#pragma once

// Golden checksums of the far horizon (see golden_support.hpp): every sample of the 10 default
// levels (windows of 65 x 65 samples) around mountains and across the corner of the wrapped
// world, where its four edges meet: heights, gradients, materials, biomes and flags.

#include "terrain/streaming/horizon_epoch.hpp"

#include "golden_support.hpp"
#include "horizon_test_support.hpp"

#include <cstdint>
#include <vector>

namespace golden {

namespace horizon_detail {

inline void addCell(Checksum& sum, const terrain::HorizonCell& cell) {
    sum.add(cell.height);
    sum.add(cell.gradX);
    sum.add(cell.gradZ);
    sum.add(static_cast<std::uint64_t>(cell.material), 1);
    sum.add(static_cast<std::uint64_t>(cell.biome), 1);
    sum.add(static_cast<std::uint64_t>(cell.flags), 1);
}

// Every level, from its canonical low corner, sample by sample.
inline std::uint64_t horizonChecksum(double x, double z) {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    terrain::HorizonClipmap clipmap(params);
    horizon_test::settle(clipmap, sampler, x, z);
    Checksum sum;
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        const terrain::HorizonRect window = level.window();
        const terrain::HorizonIndex corner =
            terrain::canonicalSample({window.x0, window.z0}, level.counts());
        sum.add(static_cast<std::uint64_t>(corner.x));
        sum.add(static_cast<std::uint64_t>(corner.z));
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                addCell(sum, level.atWindow(i, k));
            }
        }
    }
    return sum.value();
}

} // namespace horizon_detail

inline std::vector<Part> horizonParts() {
    using horizon_detail::horizonChecksum;
    const terrain::WorldSize world;
    return {
        {"horizon around mountains", horizonChecksum(horizon_test::kX, horizon_test::kZ),
         0x11E2BE2F5A504D0CULL},
        {"horizon at the world corner", horizonChecksum(world.x / 2.0, world.z / 2.0),
         0x92A9A029FB715577ULL},
    };
}

} // namespace golden
