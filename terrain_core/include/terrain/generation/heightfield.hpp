#pragma once

#include <cstdint>

namespace terrain {

// Seed shared by the export tool and the viewer, so both show the same world.
inline constexpr std::uint64_t kDefaultWorldSeed = 20261019;

// Altitudes are in meters; the sea surface is at y = kSeaLevel.
inline constexpr float kSeaLevel = 0.0f;

// Altitude range of the continental curve: without mountains, the ground stays within
// [kMinBaseHeight - detail, kMaxBaseHeight + detail].
inline constexpr float kMinBaseHeight = -2500.0f;
inline constexpr float kMaxBaseHeight = 600.0f;

// Wavelengths are in meters (one noise lattice cell of the first octave).
struct HeightfieldParams {
    // Continentalness: fbm with 4 octaves at a very low frequency. Land where it is above
    // coastContinentalness, ocean below.
    double continentWavelength = 60000.0;
    float coastContinentalness = 0.0f;

    // Macro continentalness: fbm with 2 octaves at a planetary wavelength, added to the local
    // continentalness as macroContinent * tanh(macro / macroContinentWidth). Away from the
    // zero lines of the fbm the term saturates at +-macroContinent: very large oceans, very
    // large continents, occasional supercontinents and inland seas, separated by macro coasts
    // whose width grows with macroContinentWidth (> 0). The local layer above keeps the
    // coasts, islands and regional variations. 0.5 gives planetary oceans and continents; 0
    // (the default) disables the layer: it is not even evaluated, so the world is
    // bit-identical to one without it.
    float macroContinent = 0.0f;
    double macroContinentWavelength = 10'000'000.0; // 10,000 km
    float macroContinentWidth = 0.1f;

    // Mountain mask: smoothstep(maskLow, maskHigh, fbm) at a medium frequency, faded in
    // from the coast so that no mountain rises from the ocean.
    double maskWavelength = 40000.0;
    float maskLow = -0.10f;
    float maskHigh = 0.20f;

    // Domain warping of the ridged noise coordinates, in meters: bends the crests.
    double warpWavelength = 15000.0;
    double warpAmplitude = 4000.0;

    // Ridged multifractal, always multiplied by the mountain mask. Its first octave draws
    // long crest lines (the chains), the next ones add spurs along them.
    double ridgeWavelength = 24000.0;
    int ridgeOctaves = 7;
    double ridgeSharpness = 4.0;
    float mountainHeight = 4000.0f;

    // Light high-frequency detail, faded out near sea level so that the coastline follows
    // the continentalness exactly.
    double detailWavelength = 200.0;
    float detailAmplitude = 3.0f;
};

// Layers of the height at one point: height = base + mountain + detail.
struct HeightSample {
    float continentalness = 0.0f;      // [-1, 1], local plus the macro term, clamped
    float macroContinentalness = 0.0f; // [-1, 1], raw macro fbm, 0 when disabled
    float mountainMask = 0.0f;         // [0, 1]
    float base = 0.0f;                 // continental curve, [kMinBaseHeight, kMaxBaseHeight]
    float mountain = 0.0f;             // [0, mountainMask * mountainHeight]
    float detail = 0.0f;               // [-detailAmplitude, detailAmplitude]
    float height = 0.0f;
};

// Multi-scale terrain height y = heightAt(x, z), in meters.
// Deterministic: the same seed, parameters and coordinates always give the same height.
// Like every fbm layer, the macro continentalness is exactly 0 at the origin, so the origin
// always lies on a macro coast: the local layer alone decides whether it is land or sea.
class Heightfield {
public:
    explicit Heightfield(std::uint64_t seed, const HeightfieldParams& params = {});

    [[nodiscard]] float heightAt(double x, double z) const;
    [[nodiscard]] HeightSample sampleAt(double x, double z) const;

    [[nodiscard]] const HeightfieldParams& params() const { return params_; }

private:
    void continentalnessAt(double x, double z, HeightSample& s) const;
    [[nodiscard]] float mountainAt(double x, double z, float mask) const;

    HeightfieldParams params_;
    std::uint64_t continentSeed_;
    std::uint64_t maskSeed_;
    std::uint64_t warpXSeed_;
    std::uint64_t warpZSeed_;
    std::uint64_t ridgeSeed_;
    std::uint64_t detailSeed_;
    std::uint64_t macroContinentSeed_;
};

} // namespace terrain
