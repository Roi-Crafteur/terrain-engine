#pragma once

// The dunes laid on the dunes (agent 34): compound and complex forms, as the real sand seas show
// them (smaller dunes over the flanks of greater ones, generation over generation: the Badain
// Jaran, the chains of the Grand Erg Oriental). A layer: crescents in rows and cells on the phases
// of the pivots (dune_phase.hpp), turned and scrambled by their own salt, of varied heights and
// widths (the widest join in curved ridges), never a long straight face: at most as tall as a base
// plus a share of the form they lie on, laid where the caller allows them (share: 0 on the slip
// faces and the crests of that form, 1 on its gentle flanks).

#include "dune_phase.hpp"

#include <cstdint>

namespace terrain::dune_layers {

struct Layer {
    double spacing = 320.0; // meters between their rows, where the pivots are 2 km apart
    double base = 6.0;      // meters of height, plus share times the height of the form below
    double share = 0.04;
    double turn = 0.0;      // turns, from the wind
    std::uint64_t salt = 7;
};

// The meters a layer adds at the point (s, p: its noises and pivots), on a form of height
// meters, where it lies by share in [0, 1].
[[nodiscard]] double layer(const Ergs::Impl& m, const dune_phase::Shared& s,
                           const dune_phase::Pivots& p, const Layer& l, double height,
                           double share);

} // namespace terrain::dune_layers
