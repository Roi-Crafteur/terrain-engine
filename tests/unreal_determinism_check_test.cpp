// Self-check of the Unreal plugin: the table of heights holds the bits that the core built by
// CMake computes (when the values of the terrain change, the table must follow in the same
// commit), the check passes on the default world and names the first probe that differs on
// another world.
#include "determinism_check.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using terrain::Heightfield;
using terrain::unreal::DeterminismReport;
using terrain::unreal::HeightProbe;

// Prints the bits computed for each probe that differs, to update the table.
void testTableMatchesCore() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    bool matching = true;
    for (const HeightProbe& probe : terrain::unreal::heightProbes()) {
        const auto bits = std::bit_cast<std::uint32_t>(field.heightAt(probe.x, probe.z));
        if (bits != probe.bits) {
            matching = false;
            std::fprintf(stderr, "probe (%.10g, %.10g): 0x%08XU expected, 0x%08XU computed\n",
                         probe.x, probe.z, static_cast<unsigned>(probe.bits),
                         static_cast<unsigned>(bits));
        }
    }
    TEST_CHECK(matching);
}

// The probes cover the planet, negative and fractional coordinates, land and sea.
void testProbesCover() {
    const auto probes = terrain::unreal::heightProbes();
    TEST_CHECK(probes.size() >= 8);
    const auto far = [](const HeightProbe& p) { return std::abs(p.x) > 2.5e7; };
    const auto negative = [](const HeightProbe& p) { return p.x < 0.0 && p.z < 0.0; };
    const auto fractional = [](const HeightProbe& p) { return p.x != std::floor(p.x); };
    const auto sea = [](const HeightProbe& p) { return std::bit_cast<float>(p.bits) < -100.0f; };
    const auto mountain = [](const HeightProbe& p) {
        return std::bit_cast<float>(p.bits) > 1000.0f;
    };
    for (const auto& property : {+far, +negative, +fractional, +sea, +mountain}) {
        TEST_CHECK(std::any_of(probes.begin(), probes.end(), property));
    }
}

void testDefaultWorldPasses() {
    const DeterminismReport report = terrain::unreal::checkDefaultWorld();
    TEST_CHECK(report.same);
    TEST_CHECK(terrain::unreal::describe(report).find("match") != std::string::npos);
}

// Another world differs: the report names the first probe that differs, with both heights.
void testOtherWorldFails() {
    const Heightfield other(terrain::kDefaultWorldSeed + 1);
    const DeterminismReport report = terrain::unreal::checkHeights(other);
    TEST_CHECK(!report.same);
    const auto probes = terrain::unreal::heightProbes();
    const auto first = std::find_if(probes.begin(), probes.end(), [&other](const HeightProbe& p) {
        return std::bit_cast<std::uint32_t>(other.heightAt(p.x, p.z)) != p.bits;
    });
    TEST_CHECK(first != probes.end());
    if (first != probes.end()) {
        TEST_CHECK(report.probe.x == first->x && report.probe.z == first->z);
        TEST_CHECK(report.actualBits ==
                   std::bit_cast<std::uint32_t>(other.heightAt(first->x, first->z)));
    }
    const std::string text = terrain::unreal::describe(report);
    TEST_CHECK(text.starts_with("this build does not generate the same world"));
    TEST_CHECK(text.find("instead of") != std::string::npos);
}

} // namespace

int main() {
    testTableMatchesCore();
    testProbesCover();
    testDefaultWorldPasses();
    testOtherWorldFails();
    return test::result();
}
