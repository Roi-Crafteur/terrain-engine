#include "coast_kinds.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cstddef>

namespace terrain {

const char* coastKindName(CoastKind kind) {
    switch (kind) {
    case CoastKind::Low:
        return "low";
    case CoastKind::Rocky:
        return "rocky";
    case CoastKind::Cliff:
        return "cliff";
    case CoastKind::Calanque:
        return "calanque";
    case CoastKind::Ria:
        return "ria";
    case CoastKind::Mountain:
        return "mountain";
    case CoastKind::Fjord:
        return "fjord";
    }
    return "unknown";
}

namespace coast_kinds {

namespace {

// Streams of the noises of the kinds.
constexpr std::uint64_t kAlternation = 0x33A1;
constexpr std::uint64_t kRoughness = 0x33A2;
constexpr double kAlternationWavelength = 300'000.0;
constexpr double kRoughnessWavelength = 90'000.0;

// The mountains reach the sea where the intensity of the relief of the hinterland passes
// kMountainLow to kMountainHigh (the mask of the massifs begins at 0.12); below, the hills and
// plateaus make cliffs from kHillsLow to kHillsHigh, moved by kHillsShift times the alternation.
constexpr float kMountainLow = 0.28f;
constexpr float kMountainHigh = 0.55f;
constexpr float kHillsLow = 0.13f;
constexpr float kHillsHigh = 0.24f;
constexpr float kHillsShift = 0.04f;
// The fjords: the mountains plunging into the sea where the latitude was glaciated, polar (the
// sine squared of the latitude) from kColdLow to kColdHigh (about 46 to 53 degrees).
constexpr double kColdLow = 0.52;
constexpr double kColdHigh = 0.64;
// The rocky coasts: the ruggedness of the coasts, moved by kRockyShift times the roughness noise,
// from kRockyLow to kRockyHigh.
constexpr float kRockyLow = 0.78f;
constexpr float kRockyHigh = 0.95f;
constexpr float kRockyShift = 0.2f;
// Among the cliffs: the calanques in hard young strata (strata from kStrataLow to kStrataHigh,
// character from kYoungLow to kYoungHigh), out of the cold; the rias where the cliffs are the most
// rugged, in crystalline rock rather than in strata (kRiaStrata of them less in strata).
constexpr float kStrataLow = 0.55f;
constexpr float kStrataHigh = 0.8f;
constexpr float kYoungLow = 0.5f;
constexpr float kYoungHigh = 0.65f;
constexpr float kRiaStrata = 0.8f;

noise::FbmOctaves slow(std::uint64_t seed, std::uint64_t stream, double wavelength,
                       const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = 3, .frequency = 1.0 / wavelength, .gain = 0.5}, world);
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    return {slow(seed, kAlternation, kAlternationWavelength, world),
            slow(seed, kRoughness, kRoughnessWavelength, world)};
}

Weights weightsAt(const Noises& noises, double x, double z, const Ground& g) {
    const float alternation = noises.alternation.fbm2D(x, z);
    const float roughness = noises.roughness.fbm2D(x, z);
    const float mountains = noise::smoothstep(kMountainLow, kMountainHigh, g.relief);
    const auto cold = static_cast<float>(
        noise::smoothstep(static_cast<float>(kColdLow), static_cast<float>(kColdHigh),
                          static_cast<float>(g.polar)));
    const float hills =
        noise::smoothstep(kHillsLow, kHillsHigh, g.relief + kHillsShift * alternation);
    const float rocky =
        noise::smoothstep(kRockyLow, kRockyHigh, g.rugged + kRockyShift * roughness);
    const float cliffs = (1.0f - mountains) * hills;
    const float flat = (1.0f - mountains) * (1.0f - hills);
    const float limestone = noise::smoothstep(kStrataLow, kStrataHigh, g.strata) *
                            noise::smoothstep(kYoungLow, kYoungHigh, g.character) * (1.0f - cold);
    const float ria = rocky * rocky * rocky * (1.0f - kRiaStrata * g.strata);
    Weights w{};
    w[static_cast<std::size_t>(CoastKind::Low)] = flat * (1.0f - rocky);
    w[static_cast<std::size_t>(CoastKind::Rocky)] = flat * rocky;
    w[static_cast<std::size_t>(CoastKind::Calanque)] = cliffs * limestone;
    w[static_cast<std::size_t>(CoastKind::Ria)] = cliffs * (1.0f - limestone) * ria;
    w[static_cast<std::size_t>(CoastKind::Cliff)] = cliffs * (1.0f - limestone) * (1.0f - ria);
    w[static_cast<std::size_t>(CoastKind::Mountain)] = mountains * (1.0f - cold);
    w[static_cast<std::size_t>(CoastKind::Fjord)] = mountains * cold;
    return w;
}

CoastKind heaviest(const Weights& weights) {
    const auto at = std::max_element(weights.begin(), weights.end()) - weights.begin();
    return static_cast<CoastKind>(at);
}

} // namespace coast_kinds

} // namespace terrain
