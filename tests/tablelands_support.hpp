#pragma once

// The worlds of the tests of the plateaus and of the karst (agent 46, tablelands.hpp): their flags
// are off by default (the relief of wave 9 is switched on all at once), so the tests switch them on.

#include "terrain/generation/heightfield.hpp"

namespace tablelands_test {

// The parameters of the default world with the plateaus and the karst switched on.
inline terrain::HeightfieldParams on() {
    terrain::HeightfieldParams p;
    p.geography.tablelands = true;
    p.geography.karst = true;
    return p;
}

// The default world with them, built once.
inline const terrain::Heightfield& field() {
    static const terrain::Heightfield f(terrain::kDefaultWorldSeed, on());
    return f;
}

} // namespace tablelands_test
