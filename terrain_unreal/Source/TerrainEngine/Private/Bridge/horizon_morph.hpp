#pragma once

namespace terrain::unreal {

// Geomorphing of the GPU horizon (phase 13, step 2): near the border of its window, a level
// blends into the surface of the next coarser one, so their seam has no crack and a band passing
// from one level to the other does not jump. The material of the GPU horizon
// (Scripts/create_horizon_gpu_material.py) computes the same in HLSL: these are its reference.

// Morph factor of the vertex (i, k) of a window of `cells` cells, the viewer at (u, v) cells from
// its low corner: 0 closer than cells / 2 - 2 - width cells to the viewer (Chebyshev), 1 from
// cells / 2 - 2 on, linear between. The window is centered within one cell of the player and the
// camera stands within one more: every vertex of the border morphs fully.
[[nodiscard]] double morphFactor(double i, double k, double u, double v, int cells, double width);

// The samples of a level whose mean the next coarser level shows at its sample (i, k) (both
// counted from the low corner of the window, whose indices are even on the coarser lattice): the
// sample itself for i and k even (on a coarser sample), else the ends of the edge of the coarser
// triangle through it: (i - 1, k) and (i + 1, k) for i odd, (i, k - 1) and (i, k + 1) for k odd,
// (i + 1, k - 1) and (i - 1, k + 1) for both (the diagonal of the coarser cells, from (2a + 2, 2b)
// to (2a, 2b + 2), as their triangles). All of them are even: the morph target takes the heights
// and the ground of the coarser texels at (i0 / 2, k0 / 2) and (i1 / 2, k1 / 2) of its window, as
// the crests lift the coarser samples by their own cells (terrain/streaming/horizon_crests.hpp);
// with point samples they are the texels of this level there, bit for bit.
struct MorphSources {
    int i0 = 0;
    int k0 = 0;
    int i1 = 0;
    int k1 = 0;

    friend bool operator==(const MorphSources&, const MorphSources&) = default;
};

[[nodiscard]] MorphSources morphSources(int i, int k);

} // namespace terrain::unreal
