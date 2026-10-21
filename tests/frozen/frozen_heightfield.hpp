#pragma once

// FROZEN REFERENCE, do not edit: the sampling of the Heightfield, of the tectonic plates and of
// the cave noise nodes as of commit 052078b, before the noise performance work, copied verbatim
// from heightfield.cpp, voronoi.cpp and cave_noise.cpp (frozen_heightfield.cpp,
// frozen_plates.cpp) over the frozen noise. The plate layout (sites, boundaries, kinds) is taken
// from a live terrain::TectonicPlates: the optimizations do not touch voronoi_layout.cpp.

#include "frozen_noise.hpp"

#include "terrain/generation/cave_noise.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/voronoi.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace frozen {

using terrain::BoundaryKind;
using terrain::HeightfieldParams;
using terrain::HeightSample;
using terrain::kMaxBaseHeight;
using terrain::kMinBaseHeight;
using terrain::Plate;
using terrain::PlateBoundary;
using terrain::PlateKind;
using terrain::PlateSample;
using terrain::VoronoiParams;
using terrain::WorldSize;

// terrain::TectonicPlates (voronoi.hpp), sampling only.
class TectonicPlates {
public:
    TectonicPlates(std::uint64_t seed, const VoronoiParams& params, const WorldSize& world = {});

    static constexpr int kMaxPlates = terrain::TectonicPlates::kMaxPlates;

    [[nodiscard]] PlateSample sampleAt(double x, double z) const;

    struct Point {
        double x;
        double z;
    };
    [[nodiscard]] Point warp(double x, double z) const;

    [[nodiscard]] int boundaryIndex(int a, int b) const;

    [[nodiscard]] const WorldSize& world() const { return world_; }
    [[nodiscard]] double cellDiameter() const { return cellDiameter_; }
    [[nodiscard]] const std::vector<Plate>& plates() const { return plates_; }
    [[nodiscard]] const std::vector<PlateBoundary>& boundaries() const { return boundaries_; }
    [[nodiscard]] const VoronoiParams& params() const { return params_; }

private:
    VoronoiParams params_;
    WorldSize world_;
    double cellDiameter_ = 0.0;
    std::vector<Plate> plates_;
    std::vector<PlateBoundary> boundaries_;
    std::vector<int> boundaryIndex_; // plates x plates, -1 when not neighbors
    std::array<std::uint64_t, 4> warpSeeds_{};
    std::uint64_t coastSeed_ = 0;
    double gateWidth_ = 0.0; // plates farther than this gap from a point do not affect it
};

// terrain::Heightfield (heightfield.hpp), sampling only.
class Heightfield {
public:
    explicit Heightfield(std::uint64_t seed, const HeightfieldParams& params = {},
                         const WorldSize& world = {});

    [[nodiscard]] float heightAt(double x, double z) const;
    [[nodiscard]] HeightSample sampleAt(double x, double z) const;

private:
    void continentalnessAt(double x, double z, HeightSample& s) const;
    [[nodiscard]] float mountainAt(double x, double z, float mask) const;

    HeightfieldParams params_;
    WorldSize world_;
    std::uint64_t continentSeed_;
    std::uint64_t maskSeed_;
    std::uint64_t warpXSeed_;
    std::uint64_t warpZSeed_;
    std::uint64_t ridgeSeed_;
    std::uint64_t detailSeed_;
    std::uint64_t macroContinentSeed_;
    std::shared_ptr<const TectonicPlates> plates_; // shared by copies, immutable
};

// terrain::CaveNoiseLattice::nodeValue (cave_noise.cpp) over the layers and node grids of a live
// lattice.
[[nodiscard]] float caveNodeValue(const terrain::CaveNoiseLattice& lattice,
                                  terrain::CaveNoise noise, std::int64_t i, std::int64_t j,
                                  std::int64_t k, const WorldSize& world);

} // namespace frozen
