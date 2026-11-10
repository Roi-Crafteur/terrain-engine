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
// after them, from the fine grid the normals read. The surface blends of the samples (agent 29)
// are summed apart, around the mountains of both reliefs, at the corner and at a forest border.

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

// The surface blends of every level (HorizonCell::blend, agent 29), from the canonical low corner
// of each window, sample by sample: a sum of its own, so that the sums above keep their bits.
inline std::uint64_t blendChecksum(const horizon_test::World& world, double x, double z) {
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    terrain::HorizonClipmap clipmap(params);
    horizon_test::settle(clipmap, sampler, x, z);
    Checksum sum;
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                const terrain::BlendTexel blend = level.atWindow(i, k).blend;
                sum.add(static_cast<std::uint64_t>(blend.biomes), 4);
                sum.add(static_cast<std::uint64_t>(blend.materials), 4);
                sum.add(static_cast<std::uint64_t>(blend.biomeWeights), 4);
                sum.add(static_cast<std::uint64_t>(blend.materialWeights), 4);
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
        // The biomes of the land of agent 49 (21 biomes, the soils of the deserts) changed the
        // biomes and materials of the samples and their blends: the parts around the mountains and
        // at the forest border (0xF30A045737AF4E10, 0x968ECAA664A122C0, 0x0F40EAB4E6432646,
        // 0xB79BC665E0D85848, 0x9C1FAB69C03C7234; blends 0x9A84A8FE3C20D313, 0xC213D78F8C3D4CB1,
        // 0x977D130A7EDBABD2 before them). The heights, the normals and the corner kept theirs.
        {"horizon around mountains", horizonChecksum(world, x, z, 2), 0xA61376FE894A6715ULL},
        {"horizon around legacy mountains",
         horizonChecksum(legacy, horizon_test::kX, horizon_test::kZ, 2), 0xEC3AD91FCB0F488BULL},
        {"horizon at the world corner", horizonChecksum(world, size.x / 2.0, size.z / 2.0, 2),
         0xA63D923300435B79ULL},
        {"horizon around mountains, point samples", horizonChecksum(world, x, z, 0),
         0x3B4460677C1BF897ULL},
        {"horizon around legacy mountains, point samples",
         horizonChecksum(legacy, horizon_test::kX, horizon_test::kZ, 0), 0x2D87003B8F15A117ULL},
        {"horizon around mountains, crest ratio 4", horizonChecksum(world, x, z, 4),
         0x02CD65988865C53DULL},
        {"detail normals around mountains, ratio 4", normalsChecksum(world, x, z, 4),
         0x7C759A756899258CULL},
        // The blend texels of 16 bytes (agent 49) changed the bytes of the blends of the 4 parts
        // below, never the blends they decode to (0xAFA8ABA724DEEE2F, 0x96E378BC6B035861,
        // 0xE68987A81F6AD875, 0x92DBFC547FFDE514 with the texels of 8 bytes). The water in the
        // texels (agent 49, PR 2) changed the 2 parts that hold the sea (0xBC746EBCF51CBE2D,
        // 0x084B5B9186401745 before it); the 2 dry ones kept theirs.
        {"horizon blends around mountains", horizon_detail::blendChecksum(world, x, z),
         0x05DF4413F589CE9CULL},
        {"horizon blends around legacy mountains",
         horizon_detail::blendChecksum(legacy, horizon_test::kX, horizon_test::kZ),
         0x92564D25F5479DBEULL},
        {"horizon blends at the world corner",
         horizon_detail::blendChecksum(world, size.x / 2.0, size.z / 2.0), 0x86D04563E7E49C45ULL},
        // A border of the tropical forest and the savanna (without the planet).
        {"horizon blends at a forest border", horizon_detail::blendChecksum(world, -26'927'500.0,
                                                                            -270'336.0),
         0x54254DFFD93E8938ULL},
    };
}

} // namespace golden
