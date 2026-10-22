#include "geography_stages.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::geography {

namespace {

// The slow noises of the relief, on the nodes of the map: the variation along the chains, the
// intramontane basins, the isolated massifs, the variation of the character, the regions of
// vallons and buttes.
constexpr double kVariationWavelength = 300'000.0;
constexpr double kBasinWavelength = 200'000.0;
constexpr double kMassifWavelength = 500'000.0;
constexpr double kCharacterWavelength = 400'000.0;
constexpr double kRegionWavelength = 200'000.0;

// Intramontane basins: where their noise passes kBasinLow to kBasinHigh, inside the cores of the
// belts (tails from kCoreLow to kCoreHigh), the relief falls by up to kBasinDepth of it.
constexpr float kBasinLow = 0.3f;
constexpr float kBasinHigh = 0.6f;
constexpr float kCoreLow = 0.5f;
constexpr float kCoreHigh = 0.8f;
constexpr float kBasinDepth = 0.55f;

// Isolated massifs where their noise passes kMassifLow to kMassifHigh.
constexpr float kMassifLow = 0.5f;
constexpr float kMassifHigh = 0.8f;

// The character of the belts (young and sharp), reached from that of the provinces as their
// tails pass kSharpLow to kSharpHigh, varied by +-kCharacterSpread.
constexpr float kBeltCharacter = 0.9f;
constexpr float kSharpLow = 0.15f;
constexpr float kSharpHigh = 0.6f;
constexpr float kCharacterSpread = 0.3f;

// Far from the chains, their tails keep up to kHillTail: hills and foothills, without mountains.
constexpr float kHillTail = 0.3f;

// The walls and shoulders of the rifts get an intensity of up to kRiftRelief, their floors none
// (the trough from kRiftFloorLow to kRiftFloorHigh).
constexpr float kRiftRelief = 0.25f;
constexpr float kRiftFloorLow = 0.4f;
constexpr float kRiftFloorHigh = 0.75f;

// The value of a tail at a node and the decays of the node it comes from, along x, z and the
// diagonals.
struct Tail {
    float value = 0.0f;
    float ax = 0.0f;
    float az = 0.0f;
    float ad = 0.0f;
};

// The belts with their tails, T(p) = max over q of belt(q) exp(-|p - q| / reach(q)), reach(q)
// the reach of the tails of the belt at q (reach itself when reaches is empty): a chamfer of two
// passes over the 8 neighbors, each node carrying the decays of the node whose tail it holds,
// done twice so that the tails cross the seams of the torus.
std::vector<float> beltTails(const Grid& grid, const std::vector<float>& belt,
                             const std::vector<float>& reaches, double reach) {
    const auto decays = [&grid](float value, double r) {
        return Tail{value, static_cast<float>(dmath::exp(-grid.sizeX / r)),
                    static_cast<float>(dmath::exp(-grid.sizeZ / r)),
                    static_cast<float>(dmath::exp(-dmath::hypot(grid.sizeX, grid.sizeZ) / r))};
    };
    const Tail all = decays(0.0f, reach);
    std::vector<Tail> t(belt.size());
    for (std::size_t k = 0; k < belt.size(); ++k) {
        if (belt[k] > 0.0f) {
            t[k] = reaches.empty()
                       ? Tail{belt[k], all.ax, all.az, all.ad}
                       : decays(belt[k], std::max(1.0, static_cast<double>(reaches[k])));
        }
    }
    const auto pull = [&](Tail& v, int i, int j, int step) {
        const Tail& n = t[grid.index(i, j)];
        const float candidate = n.value * (step == 0 ? n.ax : step == 1 ? n.az : n.ad);
        if (candidate > v.value) {
            v = {candidate, n.ax, n.az, n.ad};
        }
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (int j = 0; j < grid.nz; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                Tail& v = t[grid.index(i, j)];
                pull(v, i - 1, j, 0);
                pull(v, i, j - 1, 1);
                pull(v, i - 1, j - 1, 2);
                pull(v, i + 1, j - 1, 2);
            }
        }
        for (int j = grid.nz - 1; j >= 0; --j) {
            for (int i = grid.nx - 1; i >= 0; --i) {
                Tail& v = t[grid.index(i, j)];
                pull(v, i + 1, j, 0);
                pull(v, i, j + 1, 1);
                pull(v, i + 1, j + 1, 2);
                pull(v, i - 1, j + 1, 2);
            }
        }
    }
    std::vector<float> out(belt.size());
    for (std::size_t k = 0; k < belt.size(); ++k) {
        out[k] = t[k].value;
    }
    return out;
}

struct Noises {
    noise::FbmOctaves variation;
    noise::FbmOctaves basins;
    noise::FbmOctaves massifs;
    noise::FbmOctaves character;
    noise::FbmOctaves regions;
};

Noises makeNoises(std::uint64_t stream, const WorldSize& world) {
    const auto fbm = [&](std::uint64_t s, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(stream, s),
                                 {.octaves = 2,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::Unrotated},
                                 world);
    };
    return {fbm(0, kVariationWavelength), fbm(1, kBasinWavelength), fbm(2, kMassifWavelength),
            fbm(3, kCharacterWavelength), fbm(4, kRegionWavelength)};
}

struct NodeInputs {
    float tail;
    float plateau;
    float provinceRelief;
    float provinceCharacter;
    float riftWalls;
};

// The intensity and character of the relief at a node of the map, (x, z).
std::array<float, 3> nodeRelief(const GeographyParams& p, const Noises& n, const NodeInputs& in,
                                double x, double z) {
    const float variation = 1.0f + p.reliefVariation * n.variation.fbm2D(x, z);
    const float basin = noise::smoothstep(kBasinLow, kBasinHigh, n.basins.fbm2D(x, z)) *
                        noise::smoothstep(kCoreLow, kCoreHigh, in.tail);
    const float belts = in.tail * variation * (1.0f - kBasinDepth * basin);
    const float plateaus = p.plateauRelief * in.plateau * variation;
    const float massifs =
        p.massifRelief * noise::smoothstep(kMassifLow, kMassifHigh, n.massifs.fbm2D(x, z));
    const float relief = std::clamp(
        std::max({belts, plateaus, massifs, in.riftWalls, in.provinceRelief, p.reliefFloor}),
        0.0f, 1.0f);
    const float sharp = noise::smoothstep(kSharpLow, kSharpHigh, in.tail);
    const float character = std::clamp(
        in.provinceCharacter + (kBeltCharacter - in.provinceCharacter) * sharp +
            kCharacterSpread * n.character.fbm2D(x, z),
        0.0f, 1.0f);
    return {relief, character, n.regions.fbm2D(x, z)};
}

} // namespace

ReliefFields reliefFields(std::uint64_t seed, const GeographyParams& params,
                          const WorldSize& world, const Grid& grid, const Tectonics& tectonics,
                          const Provinces& provinces, int threads) {
    const std::vector<float> near = beltTails(grid, tectonics.belt, tectonics.reach, 1.0);
    std::vector<float> tails = beltTails(grid, tectonics.belt, {}, params.beltReach);
    for (std::size_t k = 0; k < tails.size(); ++k) {
        tails[k] = std::max(near[k], kHillTail * tails[k]);
    }
    const Noises noises = makeNoises(noise::deriveSeed(seed, kRelief), world);
    ReliefFields out;
    out.relief.assign(grid.nodes(), 0.0f);
    out.character.assign(grid.nodes(), 0.0f);
    out.regions.assign(grid.nodes(), 0.0f);
    planet::parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t k = grid.index(i, j);
                const float walls =
                    kRiftRelief * tectonics.shoulder[k] *
                    (1.0f - noise::smoothstep(kRiftFloorLow, kRiftFloorHigh, tectonics.trough[k]));
                const std::array<float, 3> r = nodeRelief(
                    params, noises,
                    {tails[k], tectonics.plateau[k], provinces.relief[k], provinces.character[k],
                     walls},
                    grid.x(i), grid.z(j));
                out.relief[k] = r[0];
                out.character[k] = r[1];
                out.regions[k] = r[2];
            }
        }
    });
    return out;
}

} // namespace terrain::geography
