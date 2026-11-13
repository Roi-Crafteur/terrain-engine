#pragma once

// The worlds of the tests of the plateaus and of the karst (agent 46, tablelands.hpp): their flags
// are off by default (the relief of wave 9 is switched on all at once, R1), so the tests switch
// them on by the names of the experimental flags of the world (experimental_flags.hpp).

#include "terrain/generation/experimental_flags.hpp"
#include "terrain/generation/heightfield.hpp"

#include "test_assert.hpp"

namespace tablelands_test {

// The parameters of the default world with the experimental flags "tablelands,karst".
inline terrain::HeightfieldParams on() {
    terrain::WorldParams world;
    const terrain::AppliedFlags applied =
        terrain::applyExperimentalFlags("tablelands,karst", world);
    TEST_CHECK(applied.text == "karst,tablelands" && applied.unknown.empty());
    return world.relief;
}

// The default world with them, built once.
inline const terrain::Heightfield& field() {
    static const terrain::Heightfield f(terrain::kDefaultWorldSeed, on());
    return f;
}

} // namespace tablelands_test
