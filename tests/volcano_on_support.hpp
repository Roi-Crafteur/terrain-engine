#pragma once

// The volcanoes of agent 45 are off by default until R1 (VolcanoParams::enabled): the tests of
// the volcanoes build their worlds with them on.

#include "terrain/generation/heightfield.hpp"

namespace volcano_test {

// params with the volcanoes on.
[[nodiscard]] inline terrain::HeightfieldParams on(terrain::HeightfieldParams params = {}) {
    params.volcanoes.enabled = true;
    return params;
}

// The placement and forms with them on (Volcanoes, built alone).
[[nodiscard]] inline terrain::VolcanoParams onVolcanoes() {
    terrain::VolcanoParams params;
    params.enabled = true;
    return params;
}

} // namespace volcano_test
