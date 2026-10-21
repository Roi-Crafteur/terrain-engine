#pragma once

// Helpers shared by the tests of the far horizon.

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_streamer.hpp"

#include <cstdint>
#include <optional>

namespace horizon_test {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// Mountains around (kX, kZ) in the legacy relief, and 2^20 m east: far from the borders of the
// coarse windows.
constexpr double kX = 1'048'576.0 + 4.0;
constexpr double kZ = -3'932'160.0 + 4.0;

// The relief and the ecology of the default world, or of the given relief.
struct World {
    terrain::Heightfield field;
    terrain::Ecology ecology{kSeed};

    World() : field(kSeed) {}
    explicit World(const terrain::HeightfieldParams& relief) : field(kSeed, relief) {}
};

// The default levels with small windows (65 x 65 samples), for fast tests.
inline terrain::HorizonParams smallParams() {
    terrain::HorizonParams params;
    params.cellsPerSide = 64;
    return params;
}

inline std::int64_t fullSamples(const terrain::HorizonParams& params) {
    const std::int64_t side = terrain::samplesPerSide(params);
    return params.levels * side * side;
}

inline void computeAll(const terrain::HorizonSampler& sampler, terrain::HorizonEpoch& epoch) {
    for (terrain::HorizonPiece& piece : epoch.pieces) {
        terrain::computePiece(sampler, piece);
    }
}

// Plans, computes and commits epochs until nothing changes; returns the samples computed.
inline std::int64_t settle(terrain::HorizonClipmap& clipmap, const terrain::HorizonSampler& sampler,
                           double x, double z) {
    std::int64_t samples = 0;
    while (std::optional<terrain::HorizonEpoch> epoch = clipmap.plan(x, z)) {
        computeAll(sampler, *epoch);
        samples += epoch->samples();
        clipmap.commit(*epoch);
    }
    return samples;
}

// Updates the streamer until no epoch is in flight after an update.
inline void settle(terrain::HorizonStreamer& streamer, double x, double z) {
    streamer.update(x, z);
    while (streamer.busy()) {
        streamer.waitIdle();
        streamer.update(x, z);
    }
}

// The same windows (canonical low corners) and the same samples in them, on every level.
inline bool sameContent(const terrain::HorizonClipmap& a, const terrain::HorizonClipmap& b) {
    for (int n = 0; n < a.levelCount(); ++n) {
        const terrain::HorizonLevel& la = a.level(n);
        const terrain::HorizonLevel& lb = b.level(n);
        const terrain::HorizonRect wa = la.window();
        const terrain::HorizonRect wb = lb.window();
        if (!(terrain::canonicalSample({wa.x0, wa.z0}, la.counts()) ==
              terrain::canonicalSample({wb.x0, wb.z0}, lb.counts()))) {
            return false;
        }
        for (int k = 0; k < la.side(); ++k) {
            for (int i = 0; i < la.side(); ++i) {
                if (!(la.atWindow(i, k) == lb.atWindow(i, k))) {
                    return false;
                }
            }
        }
    }
    return true;
}

inline bool sameAsFresh(const terrain::HorizonClipmap& clipmap,
                        const terrain::HorizonSampler& sampler, double x, double z) {
    terrain::HorizonClipmap fresh(clipmap.params());
    settle(fresh, sampler, x, z);
    return sameContent(clipmap, fresh);
}

} // namespace horizon_test
