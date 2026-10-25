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

    // ridged2D, with the ridged sum of its first `head` octaves alone (over their own total
    // amplitude, as a noise of `head` octaves would give it) and the gradient of that sum per
    // meter along x and z, for the slope of the broad shapes of the noise: the weights of the
    // octaves taken as constants, so that it only turns over on the creases. The weight of each
    // octave by the signal of the one before never falls below floor in [0, 1]: the valleys
    // keep that share of the finer octaves (secondary crests). With a floor of 0, the same bits
    // as ridged2D for the value. Without a memo, or through one.
    struct RidgedSlope {
        float value = 0.0f;
        float head = 0.0f;
        double headX = 0.0;
        double headZ = 0.0;
        // The least |noise| of the head octaves: 0 on the creases of one of them, where the
        // gradient turns over.
        float crease = 1.0f;
        // The fbm of the head octaves alone, as fbm2D of a noise of `head` octaves gives it: the
        // bits of fbm2D when head is at least octaves().
        float headFbm = 0.0f;
    };
    [[nodiscard]] RidgedSlope ridged2DSlope(double x, double z, double sharpness, double floor,
                                            int head) const;
    [[nodiscard]] RidgedSlope ridged2DSlope(double x, double z, double sharpness, double floor,
                                            int head, Memo& memo) const;

    // True if the twin has the same octaves but for their seeds: built with the same parameters
    // and world and another seed.
    [[nodiscard]] bool twinOf(const FbmOctaves& twin) const;

    // fbm2D of this noise and of a twin (twinOf) at one point, the position in the cells of
    // each octave computed once for both: the same bits as the two fbm2D. Without memos, or
    // through one memo each.
    [[nodiscard]] std::array<float, 2> fbm2DPair(const FbmOctaves& twin, double x,
                                                 double z) const;
    [[nodiscard]] std::array<float, 2> fbm2DPair(const FbmOctaves& twin, double x, double z,
                                                 Memo& memo, Memo& twinMemo) const;

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
    // ridged2DSlope, with the gradients of the corners of the cell of octave i at a position
    // given by cells(i, octave, position).
    template <class Cells>
    [[nodiscard]] RidgedSlope ridgedSlopeWith(double x, double z, double sharpness, double floor,
                                              int head, Cells&& cells) const;
    // The pair, with the noises of octave i at a position given by noise(i, octave, twinOctave,
    // position).
    template <class Noise>
    [[nodiscard]] std::array<float, 2> pairWith(const FbmOctaves& twin, double x, double z,
                                                Noise&& noise) const;

    std::vector<Octave> octaves_;
    double total_ = 0.0;
    WorldSize world_;
};

} // namespace terrain::noise
