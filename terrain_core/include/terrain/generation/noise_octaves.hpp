#pragma once

#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain::noise {

// Indices of the gradients of the 4 corners of a lattice cell (noise.cpp).
using CellGradients = std::array<std::uint8_t, 4>;

// The last cell (iu, iv) of the lattice of an octave met by a sequence of samples, with the
// gradients of its corners, which only depend on the cell, the lattice and the seed.
struct CellMemo {
    std::int64_t iu = 0;
    std::int64_t iv = 0;
    bool valid = false;
    CellGradients gradients{};
};

// The octaves of a noise of fbm2D, ridged2D or fbm3D, computed once: the seed, the lattice, the
// amplitude and the frequency of each octave, and their total amplitude, by the same operations
// as these functions, which rebuild them at every call. Each function below gives exactly the
// bits of the free function with the same seed, parameters and world.
class FbmOctaves {
public:
    FbmOctaves() = default; // no octave: every function gives 0
    FbmOctaves(std::uint64_t seed, const FbmParams& params, const WorldSize& world);

    [[nodiscard]] float fbm2D(double x, double z) const;
    [[nodiscard]] float ridged2D(double x, double z, double sharpness) const;
    [[nodiscard]] float fbm3D(double x, double y, double z) const;

    // What a sequence of 2D samples reuses, for one thread, during one sequence (one call of a
    // batch function): a sample in the same lattice cell of an octave as the previous one
    // reuses the gradients of its corners. The first kMemoOctaves octaves are memoized.
    static constexpr int kMemoOctaves = 16;
    struct Memo {
        std::array<CellMemo, kMemoOctaves> cells{};
    };

    // fbm2D and ridged2D through a memo: the same bits, whatever the memo holds.
    [[nodiscard]] float fbm2D(double x, double z, Memo& memo) const;
    [[nodiscard]] float ridged2D(double x, double z, double sharpness, Memo& memo) const;

    [[nodiscard]] int octaves() const { return static_cast<int>(octaves_.size()); }

private:
    struct Octave {
        std::uint64_t seed;
        Lattice2D lattice;
        double amplitude;
        double frequency; // before the adjustment of the lattice: the one along y in 3D
        double offsetY;   // in cells, 3D only
    };

    // The fbm2D and ridged2D sums over the octaves, with the noise of octave i given by
    // noise(i, octave, x, z) at the canonical point (x, z).
    template <class Noise>
    [[nodiscard]] float fbm2DWith(double x, double z, Noise&& noise) const;
    template <class Noise>
    [[nodiscard]] float ridged2DWith(double x, double z, double sharpness, Noise&& noise) const;

    std::vector<Octave> octaves_;
    double total_ = 0.0;
    WorldSize world_;
};

} // namespace terrain::noise
