#pragma once

// The insides of the Hydrology (hydrology.hpp), shared by hydrology.cpp and hydrology_query.cpp.

#include "fine_lake.hpp"
#include "river_course.hpp"
#include "river_profile.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace terrain {

namespace water_detail {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);
// The cells searched around a point for the reaches that may carve it: kSearch on each side
// (the valley of a reach reaches at most water::kReach from its course, its course at most the
// ladder and the smoothing from its points, in cells next to each other).
constexpr int kSearch = 2;
// The course of a reach strays at most kStray meters from the straight line between its points.
constexpr double kStray = 2'000.0;
constexpr double kValleyReach = 6'000.0; // water::kReach
// The meanders: half waves of kHalfWave widths of the river.
constexpr double kHalfWave = 5.5;
// The shards of the locks of the caches.
constexpr std::size_t kShards = 64;

// Ranks of the set bits of a mask of the cells.
struct Bits {
    std::vector<std::uint64_t> words;
    std::vector<std::uint32_t> rank; // set bits before each word

    void build(std::size_t cells) { words.assign((cells + 63) / 64, 0); }
    void set(std::size_t c) { words[c >> 6] |= std::uint64_t{1} << (c & 63); }
    void finish() {
        rank.resize(words.size());
        std::uint32_t total = 0;
        for (std::size_t w = 0; w < words.size(); ++w) {
            rank[w] = total;
            total += static_cast<std::uint32_t>(std::popcount(words[w]));
        }
    }
    [[nodiscard]] bool has(std::size_t c) const {
        return ((words[c >> 6] >> (c & 63)) & 1U) != 0;
    }
    [[nodiscard]] std::size_t rankOf(std::size_t c) const {
        const std::uint64_t below = (std::uint64_t{1} << (c & 63)) - 1;
        return rank[c >> 6] + static_cast<std::size_t>(std::popcount(words[c >> 6] & below));
    }
};

// Objects computed once on demand, by index, from any thread: the first to need one computes it
// (under the lock of its shard), every other reads it.
template <class T>
class Lazy {
public:
    void resize(std::size_t n) {
        slots_ = std::make_unique<std::atomic<const T*>[]>(n);
        for (std::size_t i = 0; i < n; ++i) {
            slots_[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    [[nodiscard]] bool has(std::size_t i) const {
        return slots_[i].load(std::memory_order_acquire) != nullptr;
    }
    template <class Make>
    const T& get(std::size_t i, Make&& make) {
        if (const T* p = slots_[i].load(std::memory_order_acquire)) {
            return *p;
        }
        std::lock_guard<std::mutex> shard(locks_[i % kShards]);
        if (const T* p = slots_[i].load(std::memory_order_acquire)) {
            return *p;
        }
        auto made = std::make_unique<const T>(make());
        const T* p = made.get();
        {
            std::lock_guard<std::mutex> own(ownedLock_);
            owned_.push_back(std::move(made));
        }
        slots_[i].store(p, std::memory_order_release);
        return *p;
    }
    [[nodiscard]] std::size_t count() const {
        std::lock_guard<std::mutex> own(ownedLock_);
        return owned_.size();
    }
    template <class Size>
    [[nodiscard]] std::size_t bytes(Size&& size) const {
        std::lock_guard<std::mutex> own(ownedLock_);
        std::size_t total = 0;
        for (const auto& p : owned_) {
            total += size(*p);
        }
        return total;
    }

private:
    std::unique_ptr<std::atomic<const T*>[]> slots_;
    std::array<std::mutex, kShards> locks_;
    mutable std::mutex ownedLock_;
    std::vector<std::unique_ptr<const T>> owned_;
};

using Path = std::vector<water::PathPoint>;

// The valleys of the reaches near a point that may carve it or fill it (Hydrology::riversAt), and
// the highest pond of their courses over it.
struct Candidate {
    std::size_t reach = kNone;
    water::Valley valley;
};
struct Candidates {
    std::array<Candidate, 2 * (2 * kSearch + 1) * (2 * kSearch + 1)> found{};
    std::size_t count = 0;
    float pond = -std::numeric_limits<float>::infinity();
};

// The lakes of the cells around a point, each once, in the order lakeAt meets them: kLakeSearch
// cells on each side.
constexpr int kLakeSearch = 2;
struct LakeList {
    std::array<std::int32_t, (2 * kLakeSearch + 1) * (2 * kLakeSearch + 1)> ids{};
    std::size_t count = 0;
};

} // namespace water_detail

using water_detail::Bits;
using water_detail::Candidates;
using water_detail::kNone;
using water_detail::LakeList;
using water_detail::Lazy;
using water_detail::Path;

// What the queries of the points of a region share (hydrology_region.cpp): its canonical box, the
// highest relief there, and for each cell of the planet level it meets (at most 2 x 2) the reaches
// that may carve some point of the box in the order candidatesAt meets them around that cell and
// the lakes of the cells around it; and the memo of the noise of the walls.
struct Hydrology::Region::Data {
    struct Reach {
        std::size_t reach = kNone;
        water::PathPoint start; // startOf(reach)
        // The box in the images of the reach (those of its points: start.x + wrapCanonical(x -
        // start.x) for the canonical x).
        double minX = 0.0;
        double minZ = 0.0;
        double maxX = 0.0;
        double maxZ = 0.0;
        bool everywhere = false; // mayReach holds at every point of the box
        // Its course and what of it may carve the box (BoxCourse), once a point needs them.
        const water::Course* course = nullptr;
        water::BoxCourse box;
    };
    struct Part {
        std::size_t cell = 0;
        std::vector<Reach> reaches;
        LakeList lakes;
    };
    double minX = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;
    double groundMin = 0.0;
    double groundMax = 0.0;
    std::vector<Part> parts;
    noise::FbmOctaves::Memo warp;
    // The valleys found at the last point (candidatesIn): those of found[0, count) only, the
    // others left from points before.
    Candidates candidates;

    // The part of the cell of a canonical point of the box where the relief is ground meters high,
    // or null.
    [[nodiscard]] Part* partOf(const PlanetGrid& grid, double x, double z, double ground) {
        if (!(x >= minX && x <= maxX && z >= minZ && z <= maxZ && ground >= groundMin &&
              ground <= groundMax)) {
            return nullptr;
        }
        if (parts.size() == 1) {
            return &parts.front();
        }
        const std::size_t cell = planetCellAt(grid, x, z);
        for (Part& p : parts) {
            if (p.cell == cell) {
                return &p;
            }
        }
        return nullptr;
    }
};

struct Hydrology::Impl {
    // The outlet of a lake into the river leaving it: from the floor of the cell of its sill (in
    // the images of the first point of the river) to that point.
    struct Outlet {
        std::uint32_t pass = 0;
        std::int32_t lake = -1;
        std::int32_t segment = -1;
        double x = 0.0;
        double z = 0.0;
    };

    std::uint64_t meanderSeed = 0;
    Heightfield relief;
    std::shared_ptr<const PlanetLevel> planet;
    HydrologyParams params;
    PlanetGrid grid;
    water::LadderParams ladder;
    Bits rivers;                        // the cells of the reaches carved
    std::vector<std::uint32_t> reaches; // per reach: its index in PlanetCells::riverCells
    Bits outletCells;                   // the cells of the sills of the outlets
    std::vector<Outlet> outlets;        // by cell: reaches reaches.size() and after
    std::vector<std::int32_t> outletOfLake; // per lake: its outlet reach, or -1
    Bits lakes;                         // the cells under a lake
    std::vector<std::int32_t> lakeOfCell; // per lake cell (rank of lakes): the lake
    std::vector<std::uint32_t> lakeStart; // per lake: its first cell in lakeCellList
    std::vector<std::uint32_t> lakeCellList; // the cells of the lakes, lake by lake
    mutable Lazy<water::FineLake> fineLakes; // per lake
    noise::FbmOctaves wallWarp; // the spurs and re-entrants of the walls of the valleys
    mutable Lazy<Path> paths;
    // The lowest relief on the axis of each reach: all along it, and over its first kStartWindow
    // meters (hydrology.cpp).
    struct ReachFloor {
        float all = 0.0f;
        float start = 0.0f;
    };
    mutable Lazy<ReachFloor> floors;
    mutable Lazy<float> startLevels; // the water where each reach starts
    mutable Lazy<water::Course> courses;

    Impl(std::uint64_t seed, const Heightfield& field, std::shared_ptr<const PlanetLevel> level,
         const HydrologyParams& p)
        : meanderSeed(noise::deriveSeed(seed, 0x11B0)), relief(field.withoutCoasts()),
          planet(std::move(level)),
          params(p), grid(planet->grid()),
          wallWarp(noise::deriveSeed(seed, 0x11B1),
                   {.octaves = 3, .frequency = 1.0 / 1'600.0,
                    .lattices = noise::OctaveLattices::RotatedFast},
                   field.world()) {}

    [[nodiscard]] bool isOutlet(std::size_t r) const { return r >= reaches.size(); }
    [[nodiscard]] const Outlet& outletOf(std::size_t r) const {
        return outlets[r - reaches.size()];
    }
    [[nodiscard]] const PlanetCells::RiverCell& cellOf(std::size_t r) const {
        return planet->cells().riverCells[reaches[r]];
    }
    [[nodiscard]] std::int32_t segmentIndex(std::size_t r) const {
        return isOutlet(r) ? outletOf(r).segment : cellOf(r).segment;
    }
    // The point of the segment where the reach starts: -1 for an outlet (it ends at point 0).
    [[nodiscard]] std::int32_t vertexIndex(std::size_t r) const {
        return isOutlet(r) ? -1 : cellOf(r).vertex;
    }
    [[nodiscard]] const RiverSegment& segmentOf(std::size_t r) const {
        return planet->rivers().segments[static_cast<std::size_t>(segmentIndex(r))];
    }
    [[nodiscard]] std::size_t reachOfCell(std::uint32_t cell) const {
        return rivers.has(cell) ? rivers.rankOf(cell) : kNone;
    }
    [[nodiscard]] std::size_t reachCount() const { return reaches.size() + outlets.size(); }
    // Both ends of a reach, in its images.
    [[nodiscard]] water::PathPoint startOf(std::size_t r) const;
    [[nodiscard]] water::PathPoint endOf(std::size_t r) const;

    void index();
    void indexOutlets();
    [[nodiscard]] std::size_t next(std::size_t r) const;
    [[nodiscard]] std::size_t previous(std::size_t r) const;
    [[nodiscard]] const Path& pathOf(std::size_t r) const;
    [[nodiscard]] const ReachFloor& floorOf(std::size_t r) const;
    [[nodiscard]] float startLevel(std::size_t r) const;
    [[nodiscard]] const water::Course& courseOf(std::size_t r) const;
    [[nodiscard]] water::Course makeCourse(std::size_t r) const;
    [[nodiscard]] water::ReachInput::Join joinOf(const water::Course& c, double x,
                                                 double z) const;
    [[nodiscard]] water::ReachEnd pointEnd(const RiverSegment& s, std::int32_t segment,
                                           std::int32_t vertex) const;
    [[nodiscard]] bool mayReach(std::size_t r, double x, double z, double radius) const;
    // The valleys near the canonical (x, z) where the relief is ground meters high; or those of a
    // region holding the point (the same, found through it, in its candidates).
    [[nodiscard]] Candidates candidatesAt(double x, double z, double ground) const;
    [[nodiscard]] const Candidates& candidatesIn(Region::Data& region, Region::Data::Part& part,
                                                 double x, double z, double ground) const;
    // What the valleys found at a point carve there, where the relief is ground meters high
    // (riversAt).
    [[nodiscard]] RiverCarve riversOf(const Candidates& near, double ground) const;
    // The part of a region for the cell (hydrology_region.cpp), or false where the images of a
    // reach do not follow the canonical points across the box.
    [[nodiscard]] bool partOf(const Region::Data& region, std::size_t cell,
                              Region::Data::Part& part) const;
    // The valley of reach r (start its startOf) at the canonical (x, z): its ponds in out, and the
    // valley kept in out where it may carve or fill the point (candidatesAt).
    void takeReach(std::size_t r, const water::PathPoint& start, const water::Course& course,
                   const water::BoxCourse* box, double x, double z, double ground,
                   water::WallWarp& warp, Candidates& out) const;
    // Whether two reaches follow each other on a river.
    [[nodiscard]] bool adjacent(std::size_t a, std::size_t b) const;
    // The lakes (hydrology_lakes.cpp): their cells, and their levels over the relief.
    void indexLakes();
    [[nodiscard]] std::int32_t lakeOf(std::size_t cell) const {
        return lakes.has(cell) ? lakeOfCell[lakes.rankOf(cell)] : -1;
    }
    [[nodiscard]] const water::FineLake& fineLakeOf(std::int32_t lake) const;
    [[nodiscard]] water::FineLake makeFineLake(std::int32_t lake) const;
    [[nodiscard]] float levelOf(std::int32_t lake) const { return fineLakeOf(lake).level(); }
    [[nodiscard]] LakeWater lakeAt(double x, double z, double ground) const;
    // The lakes of the cells around a cell, and the lake of a list over the canonical (x, z).
    [[nodiscard]] LakeList lakesAround(std::size_t cell) const;
    [[nodiscard]] LakeWater lakeIn(const LakeList& around, double x, double z,
                                   double ground) const;
    // The relief at a point, or the water of the lake over it: the surface the rivers flow on.
    [[nodiscard]] float surfaceAt(double x, double z, float ground) const {
        const LakeWater w = lakeAt(x, z, static_cast<double>(ground));
        return w.lake >= 0 ? std::max(ground, w.level) : ground;
    }
    // The reaches of a cell: its river reach and the outlet of its sill, if any.
    struct CellReaches {
        std::array<std::size_t, 2> reach{};
        std::size_t count = 0;
    };
    [[nodiscard]] CellReaches reachesOfCell(std::size_t cell) const;
};

} // namespace terrain
