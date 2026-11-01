#pragma once

// The lattices of the inlets and stacks of the coasts (agent 33, coast.hpp), internal to
// terrain_core: what each cell holds (a pure function of the world and the cell), built on demand
// from any thread and kept, and what they make of a point or list in a box. The large lattice
// holds the fjords and the rias, the small one the calanques, the coves and the stacks; nothing
// of a cell reaches beyond the cells around it.

#include "coast_inlets.hpp"
#include "terrain/generation/coast.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace terrain::coast_lattice {

inline constexpr std::array<double, 2> kCellMeters{8'000.0, 2'500.0};

// What a cell of a lattice holds.
struct Cell {
    std::vector<coast_inlets::Inlet> inlets;
    std::vector<coast_inlets::Stack> stacks;
};

// The cell (i, j) of a lattice, its corner (x0, z0) and its size: at most one candidate, drawn in
// it and kept near the coasts, an inlet or a stack by the kinds of coast there.
[[nodiscard]] Cell cellOf(const coast_inlets::World& w, coast_inlets::Lattice lattice,
                          std::int64_t i, std::int64_t j, double x0, double z0, double width,
                          double height);

// Objects made once by key, from any thread: the first to need one makes it (outside the lock,
// a pure function of the key), the first stored is kept and every other reads it.
template <class T>
class Store {
public:
    template <class Make>
    const T& get(std::uint64_t key, Make&& make) {
        Shard& s = shards_[key % kShards];
        {
            const std::lock_guard<std::mutex> lock(s.mutex);
            const auto it = s.map.find(key);
            if (it != s.map.end()) {
                return *it->second;
            }
        }
        auto made = std::make_unique<const T>(make());
        const std::lock_guard<std::mutex> lock(s.mutex);
        return *s.map.try_emplace(key, std::move(made)).first->second;
    }
    [[nodiscard]] std::size_t size() {
        std::size_t n = 0;
        for (Shard& s : shards_) {
            const std::lock_guard<std::mutex> lock(s.mutex);
            n += s.map.size();
        }
        return n;
    }

private:
    static constexpr std::size_t kShards = 64;
    struct Shard {
        std::mutex mutex;
        std::unordered_map<std::uint64_t, std::unique_ptr<const T>> map;
    };
    std::array<Shard, kShards> shards_;
};

// The inlets and stacks of a world.
class Lattices {
public:
    explicit Lattices(coast_inlets::World world);

    // What they make of a canonical point.
    [[nodiscard]] coast_inlets::Effect effectAt(double x, double z) const;
    // The inlets whose site lies in the box (canonical, not wrapping), sorted by x, then z, then
    // their other fields.
    [[nodiscard]] std::vector<CoastSite> sitesIn(double minX, double minZ, double maxX,
                                                 double maxZ) const;
    // The stacks whose middle lies in the box (canonical, not wrapping), sorted by x, then z.
    [[nodiscard]] std::vector<CoastStack> stacksIn(double minX, double minZ, double maxX,
                                                   double maxZ) const;
    // The cells made so far, of both lattices.
    [[nodiscard]] std::size_t cellCount() const;

private:
    struct Grid {
        std::int64_t nx = 1;
        std::int64_t nz = 1;
        double width = 0.0;
        double height = 0.0;
    };
    struct Near {
        std::vector<const coast_inlets::Inlet*> inlets;
        std::vector<const coast_inlets::Stack*> stacks;
    };
    [[nodiscard]] std::int64_t column(int lattice, double x) const;
    [[nodiscard]] std::int64_t row(int lattice, double z) const;
    [[nodiscard]] const Cell& cell(int lattice, std::int64_t i, std::int64_t j) const;
    [[nodiscard]] const Near& near(int lattice, std::int64_t i, std::int64_t j) const;

    coast_inlets::World world_;
    std::array<Grid, 2> grids_;
    mutable Store<Cell> cells_;
    mutable Store<Near> nears_;
};

} // namespace terrain::coast_lattice
