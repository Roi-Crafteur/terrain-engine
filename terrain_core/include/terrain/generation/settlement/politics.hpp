#pragma once

#include "terrain/generation/settlement/expansion.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"
#include "terrain/generation/settlement/suitability.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The rulers and the relations of the states at the start (docs/PEUPLEMENT.md, section 5): one
// ruler per state, named in the culture of its people, of one of the governments of its people,
// with a personality drawn in the ranges of its people and one to three goals from the situation of
// its state; between two neighboring states an asymmetric relation (the opinion of the peoples,
// selfRelation between two states of one people) and an asymmetric tension in [0, 1] (common
// border, coveted resources, ambition of the ruler, demographic pressure). No history, no religion.
namespace terrain::settlement {

struct Ruler {
    std::string id;         // ruler_<state>
    std::string kingdom;    // the name of its state
    std::string government; // one of its people
    std::string name;       // title, name and regnal number
    std::array<double, 5> personality{}; // by kRulerTraits, in the ranges of its people
    std::vector<std::string> goals;      // one to three
};

// What a state thinks of a neighbor (from -> to): the relation of their peoples and the tension,
// with the terms of the tension.
struct StateRelation {
    std::size_t from = 0;
    std::size_t to = 0;
    double relation = 0.0; // [-1, 1]
    double tension = 0.0;  // [0, 1]
    double border = 0.0;   // the share of the land border of from that runs along to
    double coveted = 0.0;  // the needs of from that to holds more of, [0, 1]
    std::size_t borderEdges = 0;
};

struct Politics {
    std::vector<Ruler> rulers;              // by state
    std::vector<StateRelation> relations;   // both ways for each pair of neighbors, sorted
};

// The rulers and the relations of the states (their people and their target in states, their land
// in owner, by land slot). The same result on every machine.
[[nodiscard]] Politics buildPolitics(const ExpansionTerrain& terrain, const SettlementMaps& maps,
                                     const SuitabilityMaps& suitability, const SettlementData& data,
                                     const std::vector<StatePlan>& states,
                                     const std::vector<std::uint16_t>& owner,
                                     std::uint64_t worldSeed);

} // namespace terrain::settlement
