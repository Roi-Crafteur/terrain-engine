#pragma once

#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <vector>

namespace terrain::noise {

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

    [[nodiscard]] int octaves() const { return static_cast<int>(octaves_.size()); }

private:
    struct Octave {
        std::uint64_t seed;
        Lattice2D lattice;
        double amplitude;
        double frequency; // before the adjustment of the lattice: the one along y in 3D
        double offsetY;   // in cells, 3D only
    };

    std::vector<Octave> octaves_;
    double total_ = 0.0;
    WorldSize world_;
};

} // namespace terrain::noise
