#pragma once

#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/kingdom_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {
class PlanetLevel;
}

// The borders of the states (docs/PEUPLEMENT.md, section 4 quater): the cleanup of the artifacts
// of the expansion, and the analysis of the land borders, the edges between two land cells of
// different owners, at least one of them a state (the shore is not a border). A border edge is
// natural when it follows a river (one of its cells is a river or a lake), a ridge
// (ExpansionTerrain's bits), the coast (BorderRule) or the edge of a hostile land (one of its
// cells is hostile, the other not: never the edge of a land merely less liked).
namespace terrain::settlement {

// The owners (by land slot, 0 wild) cleaned of the artifacts only, each pass read from the owners
// before it so that the order does not matter: an enclave of at most rule.holeCells cells (of the
// wild or of a state, without a capital) whose land neighbors all belong to one other owner goes to
// it (the holes, and the crumbs of a state); then a cell of a state, not a capital, with at least 3
// land neighbors (of 4), at most 1 of its own state and at least 3 of one other owner goes to that
// owner (the spikes and the filaments of one cell). Islands, isolated valleys and large enclaves
// stay.
[[nodiscard]] std::vector<std::uint16_t> cleanOwners(const ExpansionTerrain& t,
                                                     const std::vector<std::uint16_t>& owner,
                                                     const std::vector<std::size_t>& capitals,
                                                     const BorderRule& rule);

// A cut of the climate: the limit between row and row + 1 where the rainfall of the land jumps
// (BorderRule::cutFactor), found on the data alone.
struct ClimateCut {
    int row = 0;
    double latitude = 0.0; // degrees, signed (negative in the south)
    double meanJumpMm = 0.0;
};

[[nodiscard]] std::vector<ClimateCut> climateCuts(const PlanetLevel& level,
                                                  const planet::LandIndex& land,
                                                  const BorderRule& rule);

// The kinds of a border edge.
inline constexpr std::uint8_t kAlongRiver = 1;
inline constexpr std::uint8_t kAlongRidge = 2;
inline constexpr std::uint8_t kAlongCoast = 4;
inline constexpr std::uint8_t kAlongHostile = 8;

// The kinds of the edge between the land cell a and its neighbor b (slots), east (dx = 1) or
// south (dz = 1) of it.
[[nodiscard]] std::uint8_t edgeKinds(const ExpansionTerrain& t, std::size_t a, std::size_t b,
                                     bool east, const BorderRule& rule);

struct BorderStats {
    std::size_t edges = 0;   // land border edges
    std::size_t natural = 0; // along a river, a ridge or the coast
    // By the first kind among river, ridge, coast, hostile land.
    std::size_t river = 0;
    std::size_t ridge = 0;
    std::size_t coast = 0;
    std::size_t hostile = 0;
    std::size_t betweenStates = 0;
    std::size_t naturalBetweenStates = 0;
    // The cuts of the climate: the border edges and all the edges between two land cells with a
    // cell in one of the two rows of a cut, and the border edges on the cut itself.
    std::size_t nearCuts = 0;
    std::size_t landEdges = 0;
    std::size_t landEdgesNearCuts = 0;
    std::size_t landEdgesNatural = 0; // the reference: what borders blind to the land would get
    std::size_t alongCuts = 0;

    [[nodiscard]] double naturalShare() const;
    [[nodiscard]] double naturalShareBetweenStates() const;
};

// The massifs of mountains: the connected regions (by the 4 sides) of at least leastCells cells of
// a barrier model (kMountainBit) outside the hostile land. Who holds them, whether two states
// meet in them on a ridge (one on each slope), and the borders that only run along the foot of a
// massif held by no one: between a state outside the mountains and a wild cell of a massif.
struct MassifStats {
    std::size_t massifs = 0;
    std::size_t cells = 0;
    std::size_t held = 0;          // cells of the massifs held by a state
    std::size_t shared = 0;        // massifs holding cells of two states or more
    std::size_t sharedCells = 0;
    std::size_t ridgeMeet = 0;     // of those, the massifs where two states meet on a ridge
    std::size_t ridgeMeetCells = 0;
    std::size_t footEdges = 0;     // land border edges along the foot of a massif held by no one
    std::size_t footNatural = 0;   // of those, the natural ones
};

[[nodiscard]] MassifStats massifStats(const ExpansionTerrain& t,
                                      const std::vector<std::uint16_t>& owner,
                                      const BorderRule& rule, int leastCells = 25);

[[nodiscard]] BorderStats borderStats(const ExpansionTerrain& t,
                                      const std::vector<std::uint16_t>& owner,
                                      const std::vector<ClimateCut>& cuts, const BorderRule& rule);

} // namespace terrain::settlement
