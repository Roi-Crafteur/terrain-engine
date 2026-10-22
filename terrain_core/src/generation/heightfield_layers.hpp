#pragma once

// The noises of the layers of the Heightfield and their memos during a batch call, internal to
// terrain_core: heightfield.cpp builds them, heightfield_geography.cpp reads those of the
// planetary geography.

#include "ravines.hpp"
#include "relief_local.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/voronoi.hpp"

namespace terrain {

// The noises of the layers, with the parameters sampleAt gives them: the same bits as the free
// functions (FbmOctaves). The detail and macro layers are only built when enabled, as they are
// only evaluated then.
struct Heightfield::Layers {
    noise::FbmOctaves continent;
    noise::FbmOctaves macroContinent;
    noise::FbmOctaves mask;
    noise::FbmOctaves warpX;
    noise::FbmOctaves warpZ;
    noise::FbmOctaves ridge;
    noise::FbmOctaves detail;
    noise::FbmOctaves geographyDetail; // the detail of the geography: unrotated lattices
    // The warp and ridged noises of the mountains of the geography: fast rotated lattices.
    noise::FbmOctaves geographyWarpX;
    noise::FbmOctaves geographyWarpZ;
    noise::FbmOctaves geographyRidge;
    relief_local::Noises local; // the hills, vallons and buttes of the geography
    // The spurs of the chains (ridged) and the ravines of the slopes.
    noise::FbmOctaves geographySpurs;
    ravines::Network ravines;
    // Mask edges finite and at least a normal float apart: smoothstep then divides by a nonzero
    // finite number, and the massif is finite, in [+0, 1] and never -0 for every value of the
    // mask noise (heightfield_shortcuts_test), so the noise can be skipped where the land
    // factor is 0.
    bool maskSkippable = false;
    bool twinWarps = false;          // warpX and warpZ are twins (FbmOctaves::twinOf)
    bool twinGeographyWarps = false; // so are geographyWarpX and geographyWarpZ
};

// The memos of the noises of the layers and of the plates during a batch call.
struct Heightfield::Memos {
    noise::FbmOctaves::Memo continent;
    noise::FbmOctaves::Memo macroContinent;
    noise::FbmOctaves::Memo mask;
    noise::FbmOctaves::Memo warpX;
    noise::FbmOctaves::Memo warpZ;
    noise::FbmOctaves::Memo ridge;
    noise::FbmOctaves::Memo detail;
    TectonicPlates::Memos plates;
    relief_local::Memos local;
    noise::FbmOctaves::Memo spurs;
    ravines::Memo ravines;
};

} // namespace terrain
