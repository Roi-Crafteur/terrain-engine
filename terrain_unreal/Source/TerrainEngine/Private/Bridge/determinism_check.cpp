#include "determinism_check.hpp"

#include <array>
#include <bit>
#include <cstdio>

namespace terrain::unreal {

namespace {

// Produced by the core built by CMake (GCC 13, no optimization) and checked by
// unreal_determinism_check_test on every ctest run.
constexpr std::array<HeightProbe, 8> kHeightProbes{{
    {0.0, 0.0, 0x00000000U},                    // 0 m: the origin lies on a coast
    {1234.5, -6789.25, 0x4358C2E5U},            // 216.76 m
    {1027.75, -33803.5, 0x449F0DD9U},           // 1272.43 m: a mountain
    {-98765.4321, 250000.125, 0xC4C1E1C0U},     // -1551.05 m: the sea floor
    {3000000.0, -1500000.0, 0x4169C867U},       // 14.61 m
    {-12345678.9, 7654321.5, 0x43B50E1CU},      // 362.11 m
    {29999999.5, 17499999.25, 0xC2906F28U},     // -72.22 m
    {-29999999.5, -17499999.25, 0x44CD8C34U},   // 1644.38 m
}};

} // namespace

std::span<const HeightProbe> heightProbes() {
    return kHeightProbes;
}

DeterminismReport checkHeights(const Heightfield& field) {
    for (const HeightProbe& probe : kHeightProbes) {
        const auto bits = std::bit_cast<std::uint32_t>(field.heightAt(probe.x, probe.z));
        if (bits != probe.bits) {
            return {false, probe, bits};
        }
    }
    return {};
}

DeterminismReport checkDefaultWorld() {
    return checkHeights(Heightfield(kDefaultWorldSeed));
}

std::string describe(const DeterminismReport& report) {
    if (report.same) {
        return "the heights of the default world match terrain_core bit for bit";
    }
    char text[256];
    std::snprintf(text, sizeof(text),
                  "this build does not generate the same world: heightAt(%.4f, %.4f) = %.9g m "
                  "(0x%08X) instead of %.9g m (0x%08X)",
                  report.probe.x, report.probe.z,
                  static_cast<double>(std::bit_cast<float>(report.actualBits)),
                  static_cast<unsigned>(report.actualBits),
                  static_cast<double>(std::bit_cast<float>(report.probe.bits)),
                  static_cast<unsigned>(report.probe.bits));
    return text;
}

} // namespace terrain::unreal
