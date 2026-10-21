#pragma once

// Timing, checksums and points of terrain_bench_noise (bench_noise.cpp).

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace bench {

// FNV-1a over the raw bits of the results, one 64-bit word at a time (a single multiply per
// value, cheap next to the functions timed): an optimization must leave it unchanged.
class Checksum {
public:
    void add(std::uint64_t value) { hash_ = (hash_ ^ value) * 0x100000001B3ULL; }
    void add(float value) { add(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value))); }
    void add(double value) { add(std::bit_cast<std::uint64_t>(value)); }
    void add(const terrain::Mesh& mesh) {
        add(static_cast<std::uint64_t>(mesh.vertices.size()));
        for (const terrain::Vec3f& v : mesh.vertices) {
            add(v.x);
            add(v.y);
            add(v.z);
        }
        for (const std::uint32_t index : mesh.indices) {
            add(static_cast<std::uint64_t>(index));
        }
    }
    void add(const terrain::TileData& tile) {
        add(tile.mesh);
        add(tile.water);
        add(tile.lava);
        add(tile.originY);
        for (const terrain::Plant& plant : tile.plants) {
            add(plant.x);
            add(plant.z);
            add(plant.y);
        }
        add(static_cast<std::uint64_t>(tile.materials.size() + tile.speleothems.size()));
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

// Deterministic coordinates in [-range, range).
class Points {
public:
    double next(double range) {
        state_ = terrain::noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) * 0x1p-53;
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_ = 20261021;
};

struct Point {
    double x;
    double z;
};

// Median and fastest of the runs of a case, seconds per run, and the checksum of its results.
struct Timing {
    double median = 0.0;
    double best = 0.0;
    std::uint64_t checksum = 0;
    bool stable = true; // every run gave the same checksum
};

// Runs work() (which returns the checksum of what it computed) `runs` times.
template <class Work>
Timing measure(int runs, Work&& work) {
    std::vector<double> seconds;
    Timing timing;
    for (int run = 0; run < runs; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t checksum = work();
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        seconds.push_back(elapsed.count());
        if (run == 0) {
            timing.checksum = checksum;
        } else if (checksum != timing.checksum) {
            timing.stable = false;
        }
    }
    std::sort(seconds.begin(), seconds.end());
    timing.median = seconds[seconds.size() / 2];
    timing.best = seconds.front();
    return timing;
}

// One line of the report: the median per item in the unit (ns, us or ms), the fastest run,
// the items per run and the checksum.
inline void report(const char* name, const Timing& t, std::size_t items, const char* unit) {
    double scale = 1e9;
    if (unit[0] == 'u') {
        scale = 1e6;
    } else if (unit[0] == 'm') {
        scale = 1e3;
    }
    const double perItem = scale / static_cast<double>(items);
    std::printf("%-40s %11.3f %-2s (best %11.3f)  x%-8zu %016" PRIx64 "%s\n", name,
                t.median * perItem, unit, t.best * perItem, items, t.checksum,
                t.stable ? "" : "  UNSTABLE");
}

inline void section(const char* title) {
    std::printf("\n== %s\n", title);
}

inline constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// Center of the searches of the tests (streaming_test_support.hpp): inland, at about 44 degrees
// north, with hundreds of cave entrances around it.
inline constexpr std::int32_t kCenterChunkX = -77'824;
inline constexpr std::int32_t kCenterChunkZ = -531'264;
inline constexpr double kCenterX = kCenterChunkX * terrain::kChunkSizeMeters;
inline constexpr double kCenterZ = kCenterChunkZ * terrain::kChunkSizeMeters;

struct Config {
    int runs = 5;
    std::size_t divisor = 1; // 4 with --quick
};

// The checksum of every case recorded during the run.
inline Checksum& total() {
    static Checksum sum;
    return sum;
}

inline void record(const char* name, const Timing& t, std::size_t items, const char* unit) {
    report(name, t, items, unit);
    total().add(t.checksum);
}

// The planet of generator 2: macro continents and tectonic plates, the world of the planet level
// before the planetary geography.
inline terrain::HeightfieldParams planetParams() {
    terrain::HeightfieldParams params;
    params.geography.enabled = false; // the layers of generator 2, as in the earlier runs
    params.macroContinent = 0.25f;
    params.plates.amplitude = 0.5f;
    return params;
}

} // namespace bench
