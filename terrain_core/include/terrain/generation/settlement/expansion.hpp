#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/settlement/kingdom_rules.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {
class PlanetLevel;
}

// The expansion of the states from their capitals (docs/PEUPLEMENT.md, sections 4 quater and
// 6 bis): one priority queue for every state, the cost of each step from the terrain, the rivers,
// the lakes, the ridges and the straits (ExpansionRule), the borders where the fronts meet.
// ExpansionTerrain holds what the expansion reads, built from the maps or by hand (the tests).
namespace terrain::settlement {

// The bits of ExpansionTerrain::mask.
inline constexpr std::uint8_t kRiverBits = 0x07;      // the river class, 0 none to 4 impassable
inline constexpr std::uint8_t kLakeBit = 0x08;        // under a lake (fresh, salt or frozen)
inline constexpr std::uint8_t kRidgeEastBit = 0x10;   // the edge to the cell i + 1 is a ridge
inline constexpr std::uint8_t kRidgeSouthBit = 0x20;  // the edge to the cell j + 1 is a ridge
inline constexpr std::uint8_t kConfluenceBit = 0x40;  // two rivers or more meet here
inline constexpr std::uint8_t kMountainBit = 0x80;    // its nearest model is a barrier model

inline constexpr std::uint32_t kNoCompartment = 0xFFFFFFFF;

// ExpansionTerrain::drainage counts the height above the drainage in steps of this many meters.
inline constexpr double kDrainageStepM = 8.0;

// The compartments of the land: the connected regions (by the 4 sides) of the dry land that is not
// hostile, bounded exactly by what makes a natural border: the rivers and the lakes, the ridges,
// the sea, the hostile land. A state filling one exactly has natural borders only.
struct Compartments {
    std::vector<std::uint32_t> id;    // by land slot: its compartment, or kNoCompartment on a limit
    std::vector<std::uint32_t> cells; // by compartment: its land cells

    [[nodiscard]] std::uint32_t of(std::size_t slot) const { return id[slot]; }
    [[nodiscard]] std::size_t memoryBytes() const;
};

struct ExpansionTerrain {
    PlanetGrid grid;
    std::shared_ptr<const planet::LandIndex> land;
    std::vector<float> cost;           // by land slot: of entering it, before its people
    std::vector<std::uint8_t> mask;    // by land slot: the bits above
    std::vector<std::uint8_t> ford;    // by land slot: the probable ford, 0 to 255
    std::vector<std::uint8_t> coastKm; // by land slot: to the sea (255: far)
    std::vector<std::uint8_t> hostile; // by land slot: 1 on hostile land (GoodWorldRule)
    // By land slot: its height above its drainage (the first river of ValleyGrowth::riverClass or
    // more, lake or sea its receivers reach), in steps of kDrainageStepM, at most 255: low on the
    // floor of a main valley, high on the slopes and the divides.
    std::vector<std::uint8_t> drainage;
    // The sea cells a state may cross (none when the sea is forbidden), numbered as land.
    std::shared_ptr<const planet::LandIndex> straits;
    Compartments compartments;

    [[nodiscard]] int riverClass(std::size_t slot) const { return mask[slot] & kRiverBits; }
    [[nodiscard]] std::size_t memoryBytes() const;
};

// The terrain of the expansion of the maps of a level, its costs made irregular by the noise of
// worldSeed (ExpansionRule::costNoise). The same bits whatever threads.
[[nodiscard]] ExpansionTerrain buildExpansionTerrain(const PlanetLevel& level,
                                                     const SettlementMaps& maps,
                                                     const SettlementConfig& config,
                                                     std::uint64_t worldSeed, int threads);

// The compartments of a terrain (its bits and its hostile land), labeled in the order of the cells:
// the same on every machine (compartments.cpp).
[[nodiscard]] Compartments computeCompartments(const ExpansionTerrain& t);

struct ExpansionResult {
    std::vector<std::uint16_t> owner;   // by land slot: 0 wild, s + 1 for the state s
    std::vector<std::uint16_t> passage; // by strait slot: the state that crossed it first
    std::vector<std::size_t> cells;     // by state: the land cells it holds
    std::size_t pushes = 0;             // entries of the queue
    double seconds = 0.0;
};

// The states grow from their capitals (cells, by state) toward their targets, at most landCap
// cells each; suitability by people of the states. Serial: the same result on every machine.
[[nodiscard]] ExpansionResult expand(const ExpansionTerrain& terrain,
                                     const SuitabilityMaps& suitability, const ExpansionRule& rule,
                                     const std::vector<StatePlan>& states,
                                     const std::vector<std::size_t>& capitals, double landCap);

} // namespace terrain::settlement
