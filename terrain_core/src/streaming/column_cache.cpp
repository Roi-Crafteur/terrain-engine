// The columns the tiles of one world share (column_cache.hpp).
#include "column_cache.hpp"

#include "tile_lattice.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace terrain::tile {

namespace {

constexpr std::int64_t kTileCells = kChunkCells;

std::int64_t floorDiv(std::int64_t v, std::int64_t m) {
    const std::int64_t q = v / m;
    return v % m != 0 && v < 0 ? q - 1 : q;
}

bool outerRow(std::int64_t i) {
    return i == 0 || i == 1 || i == kTileCells - 1;
}

// The place of the cell (i, k) of a tile in its block, or -1 inside its outer rows.
constexpr std::array<std::int16_t, kTileCells * kTileCells> kSlots = [] {
    std::array<std::int16_t, kTileCells * kTileCells> slots{};
    std::int16_t next = 0;
    for (std::int64_t k = 0; k < kTileCells; ++k) {
        for (std::int64_t i = 0; i < kTileCells; ++i) {
            const bool shared =
                i == 0 || i == 1 || i == kTileCells - 1 || k == 0 || k == 1 || k == kTileCells - 1;
            slots[static_cast<std::size_t>(k * kTileCells + i)] = shared ? next++ : -1;
        }
    }
    return slots;
}();

bool sameTile(const ColumnCache::Point& a, const ColumnCache::Point& b) {
    return a.tileX == b.tileX && a.tileZ == b.tileZ && a.spacing == b.spacing;
}

std::size_t shardOf(const ColumnCache::Point& p) {
    std::uint64_t h = noise::deriveSeed(static_cast<std::uint64_t>(p.tileX),
                                        static_cast<std::uint64_t>(p.spacing));
    h = noise::deriveSeed(h, static_cast<std::uint64_t>(p.tileZ));
    return static_cast<std::size_t>(h % kColumnShards);
}

// The points grouped by tile: [from, to) ranges of them.
std::vector<std::pair<std::size_t, std::size_t>> groupsOf(std::vector<ColumnCache::Point>& p) {
    std::stable_sort(p.begin(), p.end(),
                     [](const ColumnCache::Point& a, const ColumnCache::Point& b) {
                         return a.tileX < b.tileX || (a.tileX == b.tileX && a.tileZ < b.tileZ);
                     });
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    for (std::size_t n = 0; n < p.size();) {
        std::size_t end = n + 1;
        while (end < p.size() && sameTile(p[end], p[n])) {
            ++end;
        }
        groups.emplace_back(n, end);
        n = end;
    }
    return groups;
}

} // namespace

bool sharedColumn(std::int64_t x, std::int64_t z, int spacing) {
    if (x % spacing != 0 || z % spacing != 0) {
        return false;
    }
    const std::int64_t i = x / spacing - kTileCells * floorDiv(x / spacing, kTileCells);
    const std::int64_t k = z / spacing - kTileCells * floorDiv(z / spacing, kTileCells);
    return outerRow(i) || outerRow(k);
}

ColumnCache::ColumnCache(std::size_t capacity, bool rock)
    : blocks_(capacity == 0 ? 0
                            : std::max<std::size_t>(1, capacity / kSharedColumns / kColumnShards)),
      rock_(rock), shards_(std::make_unique<std::array<Shard, kColumnShards>>()) {}

std::unique_lock<std::mutex> ColumnCache::lock(Shard& shard) {
    locks_.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::mutex> held(shard.mutex, std::try_to_lock);
    if (!held.owns_lock()) {
        contended_.fetch_add(1, std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        held.lock();
        const auto waited = std::chrono::steady_clock::now() - start;
        waitNanoseconds_.fetch_add(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(waited).count()),
            std::memory_order_relaxed);
    }
    return held;
}

void ColumnCache::allocate(Shard& shard) const {
    if (!shard.heads.empty()) {
        return;
    }
    shard.heads.resize(blocks_);
    shard.present.assign(blocks_ * kSharedColumns, 0);
    shard.samples.resize(blocks_ * kSharedColumns);
    if (rock_) {
        shard.rocks.resize(blocks_ * kSharedColumns);
    }
}

std::size_t ColumnCache::blockOf(Shard& shard, const Point& p, bool create) {
    for (std::size_t b = 0; b < shard.used; ++b) {
        if (sameTile(shard.heads[b], p)) {
            return b;
        }
    }
    if (!create) {
        return blocks_;
    }
    allocate(shard); // on its first use only
    std::size_t b = shard.used;
    if (shard.used < blocks_) {
        ++shard.used;
    } else {
        // All in use: the oldest block gives way.
        b = shard.next;
        shard.next = (shard.next + 1) % blocks_;
        const auto first = shard.present.begin() + static_cast<std::ptrdiff_t>(b * kSharedColumns);
        const auto last = first + static_cast<std::ptrdiff_t>(kSharedColumns);
        shard.held -= static_cast<std::size_t>(std::count(first, last, std::uint8_t{1}));
        std::fill(first, last, std::uint8_t{0});
    }
    shard.heads[b] = p;
    return b;
}

void ColumnCache::find(std::span<const Point> points, std::span<HeightSample> out,
                       std::span<RockColumn> rock, std::vector<char>& found) {
    std::vector<Point> sorted(points.begin(), points.end());
    std::size_t hits = 0;
    for (const auto& [from, to] : groupsOf(sorted)) {
        Shard& shard = (*shards_)[shardOf(sorted[from])];
        const auto held = lock(shard);
        const std::size_t b = blockOf(shard, sorted[from], false);
        if (b == blocks_) {
            continue;
        }
        for (std::size_t n = from; n < to; ++n) {
            const std::size_t at = b * kSharedColumns + sorted[n].slot;
            if (shard.present[at] == 0) {
                continue;
            }
            out[sorted[n].index] = shard.samples[at];
            if (!rock.empty()) {
                rock[sorted[n].index] = shard.rocks[at];
            }
            found[sorted[n].index] = 1;
            ++hits;
        }
    }
    lookups_.fetch_add(points.size(), std::memory_order_relaxed);
    hits_.fetch_add(hits, std::memory_order_relaxed);
}

void ColumnCache::keep(std::span<const Point> points, std::span<const char> found,
                       std::span<const HeightSample> out, std::span<const RockColumn> rock) {
    std::vector<Point> sorted;
    for (const Point& p : points) {
        if (found[p.index] == 0) {
            sorted.push_back(p);
        }
    }
    for (const auto& [from, to] : groupsOf(sorted)) {
        Shard& shard = (*shards_)[shardOf(sorted[from])];
        const auto held = lock(shard);
        const std::size_t b = blockOf(shard, sorted[from], true);
        for (std::size_t n = from; n < to; ++n) {
            const std::size_t at = b * kSharedColumns + sorted[n].slot;
            if (shard.present[at] != 0) {
                continue; // kept meanwhile by another thread: the same bits
            }
            shard.samples[at] = out[sorted[n].index];
            if (rock_) {
                shard.rocks[at] = rock[sorted[n].index];
            }
            shard.present[at] = 1;
            ++shard.held;
        }
    }
}

void ColumnCache::samplesAt(const Heightfield& field, int spacing, std::span<const double> xs,
                            std::span<const double> zs, std::span<HeightSample> out,
                            std::span<RockColumn> rock, float leastRun) {
    const bool withRock = !rock.empty();
    std::vector<Point> shared;
    for (std::size_t i = 0; blocks_ > 0 && withRock == rock_ && i < xs.size(); ++i) {
        const auto x = static_cast<std::int64_t>(xs[i]);
        const auto z = static_cast<std::int64_t>(zs[i]);
        if (static_cast<double>(x) != xs[i] || static_cast<double>(z) != zs[i] ||
            !sharedColumn(x, z, spacing)) {
            continue;
        }
        const std::int64_t tileX = floorDiv(x / spacing, kTileCells);
        const std::int64_t tileZ = floorDiv(z / spacing, kTileCells);
        const std::int64_t cell =
            (z / spacing - kTileCells * tileZ) * kTileCells + (x / spacing - kTileCells * tileX);
        shared.push_back({tileX, tileZ, spacing,
                          static_cast<std::uint32_t>(kSlots[static_cast<std::size_t>(cell)]), i});
    }
    std::vector<char> found(xs.size(), 0);
    if (!shared.empty()) {
        find(shared, out, rock, found);
    }
    // The others in one batch (each sample the same bits in any batch).
    std::vector<double> missX;
    std::vector<double> missZ;
    std::vector<std::size_t> missing;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (found[i] == 0) {
            missing.push_back(i);
            missX.push_back(xs[i]);
            missZ.push_back(zs[i]);
        }
    }
    if (!missing.empty()) {
        std::vector<HeightSample> samples(missing.size());
        std::vector<RockColumn> rocks(withRock ? missing.size() : 0);
        if (withRock) {
            field.samplesAt(missX, missZ, samples, rocks, leastRun);
        } else {
            field.samplesAt(missX, missZ, samples);
        }
        for (std::size_t n = 0; n < missing.size(); ++n) {
            out[missing[n]] = samples[n];
            if (withRock) {
                rock[missing[n]] = rocks[n];
            }
        }
    }
    if (!shared.empty()) {
        keep(shared, found, out, rock);
    }
}

std::size_t ColumnCache::bytesOf(const Shard& shard) const {
    return shard.heads.capacity() * sizeof(Point) + shard.present.capacity() +
           shard.samples.capacity() * sizeof(HeightSample) +
           shard.rocks.capacity() * sizeof(RockColumn);
}

std::size_t ColumnCache::bytesBound() const {
    const std::size_t column =
        1 + sizeof(HeightSample) + (rock_ ? sizeof(RockColumn) : std::size_t{0});
    return kColumnShards * blocks_ * (sizeof(Point) + kSharedColumns * column);
}

ColumnCacheStats ColumnCache::stats() const {
    ColumnCacheStats s;
    s.capacity = kColumnShards * blocks_ * kSharedColumns;
    s.bytesBound = bytesBound();
    for (Shard& shard : *shards_) {
        const std::lock_guard held(shard.mutex);
        s.columns += shard.held;
        s.bytes += bytesOf(shard);
    }
    s.lookups = lookups_.load(std::memory_order_relaxed);
    s.hits = hits_.load(std::memory_order_relaxed);
    s.locks = locks_.load(std::memory_order_relaxed);
    s.contended = contended_.load(std::memory_order_relaxed);
    s.waitSeconds = static_cast<double>(waitNanoseconds_.load(std::memory_order_relaxed)) * 1e-9;
    return s;
}

void sampleThrough(const Heightfield& field, ColumnCache* cache, int spacing,
                   std::span<const double> xs, std::span<const double> zs,
                   std::span<HeightSample> out, std::span<RockColumn> rock, float leastRun) {
    if (cache != nullptr) {
        cache->samplesAt(field, spacing, xs, zs, out, rock, leastRun);
    } else if (!rock.empty()) {
        field.samplesAt(xs, zs, out, rock, leastRun);
    } else {
        field.samplesAt(xs, zs, out);
    }
}

} // namespace terrain::tile
