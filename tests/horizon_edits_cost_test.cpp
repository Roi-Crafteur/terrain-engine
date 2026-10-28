// Cost of the player edits on the far horizon, with the default levels (K = 256, 10 levels, the
// crests): for a crater of 10 m, a big explosion of 64 m, a mountain razed by a box of 1 km x 1 km
// x 500 m and the same razed by the box and 47,000 explosions. Printed: the time of the edit in
// the tiles, of the edited ground of its columns (stage), of their summaries (applyStaged), of
// the epoch that computes the samples around it again on 1 and 2 threads, the samples and the
// bytes of texels it sends, and the memory of the map. Asserted: the epoch only computes again
// the samples around the edit; and, loosely so that a slow machine passes, that a big explosion
// shows far away within 1 s (the edit, its columns, its summaries and its epoch on 2 threads).
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <thread>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::SequencedEdit;
using terrain::TerrainTiles;
using terrain::VolumeEdit;

class Stopwatch {
public:
    [[nodiscard]] double milliseconds() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

VolumeEdit explosion(BlockCoord center, double radius) {
    return {EditShape::Explosion,
            EditMode::Remove,
            center,
            {},
            static_cast<std::uint16_t>(radius * 16.0)};
}

// The default levels placed around (x, z) without computing them: the cells of the epochs are
// left empty, only the windows matter here.
terrain::HorizonClipmap placedAround(const terrain::HorizonParams& params, double x, double z) {
    terrain::HorizonClipmap clipmap(params);
    while (std::optional<terrain::HorizonEpoch> epoch = clipmap.plan(x, z)) {
        for (terrain::HorizonPiece& piece : epoch->pieces) {
            piece.cells.resize(static_cast<std::size_t>(piece.rect.count()));
        }
        clipmap.commit(*epoch);
    }
    return clipmap;
}

// Computes the pieces of an epoch on `threads` threads; returns the milliseconds it took.
double compute(const terrain::HorizonSampler& sampler, terrain::HorizonEpoch& epoch, int threads) {
    for (terrain::HorizonPiece& piece : epoch.pieces) {
        piece.cells.clear();
    }
    const Stopwatch watch;
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (std::size_t n = next++; n < epoch.pieces.size(); n = next++) {
            terrain::computePiece(sampler, epoch.pieces[n]);
        }
    };
    std::vector<std::thread> workers;
    for (int t = 1; t < threads; ++t) {
        workers.emplace_back(work);
    }
    work();
    for (std::thread& worker : workers) {
        worker.join();
    }
    return watch.milliseconds();
}

// The epoch after an edit for an observer at some distance from it.
struct Epoch {
    double distance = 0.0;
    std::int64_t samples = 0;
    double oneThreadMs = 0.0;
    double twoThreadsMs = 0.0;
};

struct Measure {
    const char* name = "";
    double editMs = 0.0;
    double stageMs = 0.0;
    double applyMs = 0.0;
    std::vector<Epoch> epochs;
    std::size_t columns = 0;
    std::size_t mapBytes = 0;
};

// Applies edits to fresh tiles, then brings them to horizons placed 600 m (level 0 holds the
// edit) and 20 km away from (x, z).
Measure measure(const char* name, double x, double z, const std::vector<VolumeEdit>& edits) {
    Measure m;
    m.name = name;
    TerrainTiles tiles(streaming_test::kSeed);
    terrain::HorizonEditMap map(tiles.heightfield());
    const terrain::HorizonParams params;
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params,
                                          map.source());
    std::vector<terrain::HorizonClipmap> clipmaps;
    for (const double distance : {600.0, 20'000.0}) {
        clipmaps.push_back(placedAround(params, x + distance, z));
        m.epochs.push_back({distance, 0, 0.0, 0.0});
    }
    std::vector<SequencedEdit> sequence;
    for (const VolumeEdit& edit : edits) {
        sequence.push_back({sequence.size(), edit});
    }
    Stopwatch watch;
    const std::vector<terrain::ChunkCoord> chunks = tiles.applyInSequence(sequence);
    m.editMs = watch.milliseconds();
    watch = Stopwatch();
    map.stage(tiles, chunks);
    m.stageMs = watch.milliseconds();
    watch = Stopwatch();
    const std::vector<terrain::HorizonArea> areas = map.applyStaged();
    m.applyMs = watch.milliseconds();
    for (std::size_t n = 0; n < clipmaps.size(); ++n) {
        for (const terrain::HorizonArea& area : areas) {
            clipmaps[n].invalidate(area.minX, area.minZ, area.maxX, area.maxZ);
        }
        std::optional<terrain::HorizonEpoch> epoch = clipmaps[n].plan(x + m.epochs[n].distance, z);
        TEST_CHECK(epoch.has_value());
        if (epoch) {
            m.epochs[n].samples = epoch->samples();
            m.epochs[n].oneThreadMs = compute(sampler, *epoch, 1);
            m.epochs[n].twoThreadsMs = compute(sampler, *epoch, 2);
            clipmaps[n].commit(*epoch);
        }
    }
    m.columns = map.columnCount();
    m.mapBytes = map.heapBytes();
    return m;
}

void print(const Measure& m) {
    std::printf("%s: edit %.1f ms, columns %.1f ms, summaries %.2f ms, %zu columns, map %.1f "
                "KiB\n",
                m.name, m.editMs, m.stageMs, m.applyMs, m.columns,
                static_cast<double>(m.mapBytes) / 1024.0);
    for (const Epoch& e : m.epochs) {
        std::printf("  epoch seen from %.1f km: %lld samples (%.1f KiB of texels), %.1f ms on 1 "
                    "thread, %.1f ms on 2\n",
                    e.distance / 1000.0, static_cast<long long>(e.samples),
                    static_cast<double>(e.samples) * 16.0 / 1024.0, e.oneThreadMs, e.twoThreadsMs);
    }
}

// The floor and walls of a razed square dug by explosions of 7 m every 8 m.
std::vector<VolumeEdit> razed(BlockCoord corner, int height, bool explosions) {
    std::vector<VolumeEdit> edits = {
        {EditShape::Box, EditMode::Remove, corner, {1000, 500, 1000}, 0}};
    for (int a = 0; explosions && a < 1000; a += 8) {
        for (int b = 0; b < 1000; b += 8) {
            edits.push_back(explosion({corner.x + a, corner.y, corner.z + b}, 7.0));
        }
        for (int y = corner.y; y < corner.y + height; y += 8) {
            edits.push_back(explosion({corner.x, y, corner.z + a}, 7.0));
            edits.push_back(explosion({corner.x + 999, y, corner.z + a}, 7.0));
            edits.push_back(explosion({corner.x + a, y, corner.z}, 7.0));
            edits.push_back(explosion({corner.x + a, y, corner.z + 999}, 7.0));
        }
    }
    return edits;
}

void testCosts() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(tiles.heightfield());
    const int x0 = mountain.x * 16 + 5;
    const int z0 = mountain.z * 16 + 7;
    int top = groundAt(tiles, x0, z0);
    for (int dz = 0; dz <= 999; dz += 25) {
        for (int dx = 0; dx <= 999; dx += 25) {
            top = std::max(top, groundAt(tiles, x0 + dx, z0 + dz));
        }
    }
    const int cx = x0 + 500;
    const int cz = z0 + 500;
    const BlockCoord center{cx, groundAt(tiles, cx, cz), cz};
    const Measure crater =
        measure("crater of 10 m", cx, cz, {{EditShape::Sphere, EditMode::Remove, center, {}, 80}});
    const Measure blast = measure("big explosion of 64 m", cx, cz, {explosion(center, 32.0)});
    const Measure box =
        measure("mountain razed by a box", cx, cz, razed({x0, top - 499, z0}, 500, false));
    const Measure blasts = measure("mountain razed by 47,000 explosions", cx, cz,
                                   razed({x0, top - 499, z0}, 500, true));
    for (const Measure& m : {crater, blast, box, blasts}) {
        print(m);
    }
    // Around the edit only: a few samples per level for the explosion, about the square for the
    // razed mountain (16,000 samples of 8 m on level 0, a quarter of that on level 1...).
    TEST_CHECK(blast.epochs[0].samples > 0 && blast.epochs[0].samples < 2'000);
    TEST_CHECK(box.epochs[0].samples > 15'000 && box.epochs[0].samples < 30'000);
    TEST_CHECK(box.epochs[1].samples < box.epochs[0].samples / 10);
    for (const Epoch& e : blast.epochs) {
        const double seen = blast.editMs + blast.stageMs + blast.applyMs + e.twoThreadsMs;
        std::printf("big explosion seen from %.1f km %.1f ms after it (no epoch in flight)\n",
                    e.distance / 1000.0, seen);
        TEST_CHECK(seen < 1000.0);
    }
}

} // namespace

int main() {
    testCosts();
    return test::result();
}
