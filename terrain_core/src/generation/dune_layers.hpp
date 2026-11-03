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
// meters, where it lies by share in [0, 1]. cover in [0, 1], if not negative: how much the form
// below wants them there (its flanks and crests 1, its corridors 0): no void of the groups
// where it is 1, kCorridorShare of them where it is 0.
[[nodiscard]] double layer(const Ergs::Impl& m, const dune_phase::Shared& s,
                           const dune_phase::Pivots& p, const Layer& l, double height,
                           double share, double cover = -1.0);

// The form below as each crescent reads it at its middle, dx, dz meters from the point: its share
// and its cover there (as above), the same over the whole crescent (read at the point, they
// changed within it, and so did its height and its width: its flanks stood at up to 46 degrees on
// the backs of the giant waves, f9c3391), and the room downwind of it before the form below falls
// away (ahead: to its next crest, where its slip face starts) and upwind (behind: to the foot of
// its last slip face): the crescent narrowed to stand within it (its horns reaching over the
// brink of a giant wave stood at 46 degrees on its face; within it, none on its slip faces).
// shareByCover: the share times the cover.
struct Host {
    const void* context = nullptr;
    void (*at)(const void* context, double dx, double dz, double& share, double& cover,
               double& ahead, double& behind) = nullptr;
    bool shareByCover = false;
};
[[nodiscard]] double layer(const Ergs::Impl& m, const dune_phase::Shared& s,
                           const dune_phase::Pivots& p, const Layer& l, double height,
                           const Host& host);

} // namespace terrain::dune_layers
