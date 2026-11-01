#pragma once

// The reveal of the levels of the GPU horizon (agent 38): a level comes in over a few tenths of a
// second when it is shown, rather than at once, and the coarsest level shown while the coarser
// ones are still to come dissolves its outer band into the sky, so that the land never ends on a
// sharp line (Scripts/horizon_reveal.py draws both). Pure C++: TerrainHorizonClipmap applies it.

#include "horizon_gpu_streamer.hpp"

#include <vector>

namespace terrain::unreal {

class HorizonReveal {
public:
    // levels of the horizon; seconds of a reveal (0 or less: at once).
    HorizonReveal(int levels, double seconds);

    // A placement applied: a level shown again starts its reveal at 0 (at 1 when instant, the
    // screen held black), a hidden level is at 0.
    void place(const LevelPlacement& placement, bool instant);
    // Time passing: the reveals go on (at once when instant). Returns the levels whose reveal or
    // edge fade changed since the last call.
    [[nodiscard]] std::vector<int> advance(double seconds, bool instant);

    // The share of the level shown, and of its outer band dissolved: 1 - the reveal of the next
    // coarser level while it is shown, 1 while it is hidden, 0 for the coarsest level.
    [[nodiscard]] double reveal(int level) const;
    [[nodiscard]] double edgeFade(int level) const;
    [[nodiscard]] bool visible(int level) const;

private:
    struct Level {
        bool visible = false;
        double reveal = 0.0;
        double sentReveal = -1.0; // as last returned by advance
        double sentEdge = -1.0;
    };

    [[nodiscard]] const Level& at(int level) const;

    std::vector<Level> levels_;
    double seconds_;
};

} // namespace terrain::unreal
