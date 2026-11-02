// The reference inlets of the coasts (agent 33), checked in Unreal and kept to the bit: the
// calanque of the stop point 2 (its site, and every height and water of the world on a grid over
// it and its walls). Pinned on 2a4553d, the commit checked; nothing that changes the other inlets
// may change it.
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"

#include "golden_support.hpp"
#include "test_assert.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::CoastSite;
using terrain::Heightfield;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// The calanque: its site as Heightfield::inletsIn lists it.
constexpr double kX = 23'417'883.0;
constexpr double kZ = -2'119'730.0;
// The grid: kSide meters around its site, every kStep meters.
constexpr double kSide = 1'800.0;
constexpr double kStep = 10.0;
constexpr std::uint64_t kSite = 0x3C91434C58964152ULL;
constexpr std::uint64_t kGrid = 0x57494FD463E1023CULL;

void testCalanque() {
    const Heightfield f(kSeed);
    const CoastSite* found = nullptr;
    const std::vector<CoastSite> sites = f.inletsIn(kX - 500.0, kZ - 500.0, kX + 500.0, kZ + 500.0);
    for (const CoastSite& s : sites) {
        if (s.inlet == terrain::InletKind::Calanque && found == nullptr) {
            found = &s;
        }
    }
    TEST_CHECK(found != nullptr);
    if (found == nullptr) {
        return;
    }
    golden::Checksum site;
    site.add(found->x);
    site.add(found->z);
    for (const float v : {found->opening, found->size, found->length, found->depth,
                          found->shelter}) {
        site.add(v);
    }
    site.add(found->harbor);
    golden::Checksum grid;
    const int n = static_cast<int>(kSide / kStep);
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const terrain::HeightSample s =
                f.sampleAt(found->x - 0.5 * kSide + i * kStep, found->z - 0.5 * kSide + j * kStep);
            grid.add(s.height);
            grid.add(s.waterLevel);
            grid.add(static_cast<int>(s.water));
        }
    }
    std::printf("calanque (%.1f, %.1f): opening %.1f, widest %.1f, length %.1f, depth %.1f, "
                "shelter %.3f; site 0x%016llXULL, grid 0x%016llXULL\n",
                found->x, found->z, found->opening, found->size, found->length, found->depth,
                found->shelter, static_cast<unsigned long long>(site.value()),
                static_cast<unsigned long long>(grid.value()));
    TEST_CHECK(site.value() == kSite);
    TEST_CHECK(grid.value() == kGrid);
}

} // namespace

int main() {
    testCalanque();
    return test::result();
}
