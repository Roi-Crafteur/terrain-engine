#pragma once

// The network of the planet level (task 11C, step 1): every land cell drains somewhere, and the
// rivers of at least 10 m^3/s (planet_rivers.hpp) are only the largest of its streams.
//
// Derived from the level when asked (the tools, the maps, the probe), never stored: the level keeps
// the discharge of every land cell (PlanetCells::landDischarge), its receivers and its lakes; the
// network follows them once downstream and once upstream:
// - down: the cell a cell drains to (its receiver), a lake standing for its whole surface (the
//   cells under it drain to its outlet, the partner of its pass), or an end: the sea, an
//   overflowing lake spilling into the sea, a terminal lake, a dry basin (a pit or a playa);
// - the order of Strahler of the graph of the cells, every cell seeded with the order its own
//   small network reaches (Horton: 1 + log of the area of the cell over the area of a channel
//   head, 4.5 times larger from an order to the next; the channel heads of the climate,
//   runoff_partition.hpp); a lake cell adds no order of its own;
// - the area it drains (cells), the basin (the cell its water ends at) and its end;
// - the watercourses (the rule of Hack: at a confluence the branch of largest discharge keeps the
//   name), their kinds by their size and their end (never drawn): a fleuve ends at the sea with at
//   least fleuveLeast m^3/s, a river carries at least riverLeast m^3/s to its end, a brook less;
//   a tributary knows the bank it joins on (right or left, looking downstream);
// - the trunks: the links of at least trunkCells cells or trunkDischarge m^3/s, the streams the
//   fine network drains to (water/network.hpp).
// Deterministic, the same whatever the threads (one pass, single-threaded, in a fixed order).

#include "terrain/generation/planet/planet_level.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {

enum class StreamKind : std::uint8_t { Brook, River, Fleuve };
enum class StreamEnd : std::uint8_t { Confluence, Sea, Lake, TerminalLake, DryBasin };
enum class StreamBank : std::uint8_t { None, Right, Left };

struct PlanetNetworkParams {
    double fleuveLeast = 100.0; // m^3/s at its mouth in the sea
    double riverLeast = 1.0;    // m^3/s at its end
    std::uint32_t trunkCells = 4;
    double trunkDischarge = 10.0;
};

class PlanetNetwork {
public:
    // The network of level, which must outlive it.
    explicit PlanetNetwork(const PlanetLevel& level, const PlanetNetworkParams& params = {});

    [[nodiscard]] const PlanetLevel& level() const { return *level_; }
    [[nodiscard]] bool land(std::size_t cell) const;
    // m^3/s leaving a land cell (0 at sea).
    [[nodiscard]] double discharge(std::size_t cell) const;
    // The cell it drains to, or -1 where its water ends (end()).
    [[nodiscard]] std::int64_t down(std::size_t cell) const;
    [[nodiscard]] StreamEnd end(std::size_t cell) const; // Confluence where it goes on
    [[nodiscard]] std::uint8_t order(std::size_t cell) const;
    [[nodiscard]] std::uint32_t area(std::size_t cell) const; // cells, saturated at 65,535
    [[nodiscard]] std::uint32_t basin(std::size_t cell) const;
    [[nodiscard]] StreamKind kind(std::size_t cell) const;
    // Whether it goes on the watercourse of the cell it drains to (its largest donor).
    [[nodiscard]] bool mainStem(std::size_t cell) const;
    // For a tributary (not mainStem), the bank of the watercourse it joins.
    [[nodiscard]] StreamBank bank(std::size_t cell) const;
    [[nodiscard]] bool trunk(std::size_t cell) const;
    // The order a cell starts with, from the channel heads of its climate.
    [[nodiscard]] std::uint8_t seed(std::size_t cell) const;
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    void build();
    // Downstream: the orders and the areas, the largest donor of each cell; the cells in order.
    [[nodiscard]] std::vector<std::uint32_t> downstream(std::vector<std::uint32_t>& best);
    // Upstream, from the ends: the basins, the watercourses, their kinds and banks.
    void upstream(const std::vector<std::uint32_t>& sequence,
                  const std::vector<std::uint32_t>& best);
    // Where the water of a land cell ends there (Confluence where it goes on; Lake where it enters
    // a lake that spills on: its watercourse ends, its water goes on from the outlet).
    [[nodiscard]] StreamEnd ownEnd(std::size_t cell) const;
    [[nodiscard]] std::size_t slot(std::size_t cell) const;

    const PlanetLevel* level_;
    PlanetNetworkParams params_;
    std::shared_ptr<const planet::LandIndex> land_;
    std::vector<std::uint8_t> order_;
    std::vector<std::uint16_t> area_;
    std::vector<std::uint32_t> basin_;
    std::vector<std::uint8_t> flags_; // kind, main stem, bank, end
};

} // namespace terrain
