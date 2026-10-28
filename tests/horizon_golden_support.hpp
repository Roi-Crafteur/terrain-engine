#pragma once

// Golden checksums of the far horizon (see golden_support.hpp): every sample of the 10 default
// levels (windows of 65 x 65 samples) around mountains of the default world (found by the
// search of streaming_test::mountainChunk) and of the legacy relief, and across the corner of the
// wrapped world, where its four edges meet: heights, gradients, materials, biomes and flags.
// The default levels raise their samples to the crests (horizon_crests.hpp); the mountains are
// also summed with point samples only (crestRatio 0), which keep the values from before the
// prefilter: the crests change the heights of the levels 1 and up, nothing else. The corner is
// deep ocean, where nothing rises. The mountains are summed again with crests sought 4 times finer
// (crestRatio 4), and the detail normals of every level around them (horizon_normals.hpp, ratio 4):
// the crests and the normals keep these bits whether the crests are computed in the epochs or
// after them, from the fine grid the normals read.

#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_normals.hpp"

#include "golden_support.hpp"
#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"

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
inline std::uint64_t horizonChecksum(const horizon_test::World& world, double x, double z,
                                     int crestRatio) {
    terrain::HorizonParams params = horizon_test::smallParams();
    params.crestRatio = crestRatio;
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

// The detail slopes of every window around (x, z), level by level, texel by texel.
inline std::uint64_t normalsChecksum(const horizon_test::World& world, double x, double z,
                                     int ratio) {
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonNormalSampler sampler(world.field, params, ratio);
    Checksum sum;
    for (int n = 0; n < params.levels; ++n) {
        const terrain::HorizonRect window = terrain::windowAround(
            terrain::canonicalCenter(params, n, x, z), params.cellsPerSide);
        for (const terrain::DetailSlope& slope : sampler.sample(n, window)) {
            sum.add(slope.gradX);
            sum.add(slope.gradZ);
        }
    }
    return sum.value();
}

} // namespace horizon_detail

inline std::vector<Part> horizonParts() {
    using horizon_detail::horizonChecksum;
    using horizon_detail::normalsChecksum;
    const horizon_test::World world;
    const horizon_test::World legacy(test_relief::legacyRelief());
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field);
    const double x = mountain.x * terrain::kChunkSizeMeters + 4.0;
    const double z = mountain.z * terrain::kChunkSizeMeters + 4.0;
    const terrain::WorldSize size;
    return {
        {"horizon around mountains", horizonChecksum(world, x, z, 2), 0xF30A045737AF4E10ULL},
        {"horizon around legacy mountains",
         horizonChecksum(legacy, horizon_test::kX, horizon_test::kZ, 2), 0x968ECAA664A122C0ULL},
        {"horizon at the world corner", horizonChecksum(world, size.x / 2.0, size.z / 2.0, 2),
         0xA63D923300435B79ULL},
        {"horizon around mountains, point samples", horizonChecksum(world, x, z, 0),
         0x0F40EAB4E6432646ULL},
        {"horizon around legacy mountains, point samples",
         horizonChecksum(legacy, horizon_test::kX, horizon_test::kZ, 0), 0xB79BC665E0D85848ULL},
        {"horizon around mountains, crest ratio 4", horizonChecksum(world, x, z, 4),
         0x9C1FAB69C03C7234ULL},
        {"detail normals around mountains, ratio 4", normalsChecksum(world, x, z, 4),
         0x7C759A756899258CULL},
    };
}

} // namespace golden
