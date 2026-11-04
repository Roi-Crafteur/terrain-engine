// The volcanoes of the default world (agent 45, volcanoes.hpp): the same positions, kinds, active
// and large ones as the geology of the settlement (data/settlement), whose phase 2 they are; the
// same bits whatever the number of threads; every form drawn, the islands and the seamounts at
// sea only; the sizes in their ranges; the buckets find every volcano that reaches a point.
#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/world_size.hpp"

#include "test_assert.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace st = terrain::settlement;

namespace {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The geology of the settlement on the coarse grid of its test (the volcanoes never depend on
// the grid: only its area, the world's).
st::Geology geologyOf(const terrain::Heightfield& field) {
    const terrain::Geography& geo = *field.geography();
    const terrain::PlanetGrid grid = terrain::makePlanetGrid(geo.world(), 65536.0);
    std::vector<std::uint32_t> cells;
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            if (geo.sampleAt(grid.centerX(i), grid.centerZ(j)).coast > 0.0) {
                cells.push_back(static_cast<std::uint32_t>(grid.index(i, j)));
            }
        }
    }
    const auto land = std::make_shared<const terrain::planet::LandIndex>(
        terrain::planet::LandIndex::ofCells(grid.cells(), cells));
    const st::SettlementConfig config = st::loadSettlementData(TERRAIN_SETTLEMENT_DATA).config;
    return st::buildGeology(grid, land, geo, kSeed, config, 0);
}

void testSameAsGeology(const terrain::Heightfield& field) {
    const st::Geology g = geologyOf(field);
    const std::vector<terrain::VolcanoSite>& sites = field.volcanoes()->sites();
    bool same = g.volcanoes.size() == sites.size();
    for (std::size_t k = 0; same && k < sites.size(); ++k) {
        const st::Volcano& v = g.volcanoes[k];
        same = v.x == sites[k].x && v.z == sites[k].z && v.active == sites[k].active &&
               v.large == sites[k].large &&
               static_cast<int>(v.kind) == static_cast<int>(sites[k].origin);
    }
    std::printf("%zu volcanoes, %zu in the geology of the settlement: %s\n", sites.size(),
                g.volcanoes.size(), same ? "the same" : "DIFFERENT");
    TEST_CHECK(same);
}

bool sameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool sameSites(const std::vector<terrain::VolcanoSite>& a,
               const std::vector<terrain::VolcanoSite>& b) {
    bool same = a.size() == b.size();
    for (std::size_t k = 0; same && k < a.size(); ++k) {
        same = a[k].x == b[k].x && a[k].z == b[k].z && a[k].form == b[k].form &&
               a[k].seed == b[k].seed && sameBits(a[k].height, b[k].height) &&
               sameBits(a[k].floor, b[k].floor) && sameBits(a[k].radius, b[k].radius) &&
               sameBits(a[k].crater, b[k].crater) && sameBits(a[k].reach, b[k].reach) &&
               sameBits(a[k].driftX, b[k].driftX) && sameBits(a[k].driftZ, b[k].driftZ);
    }
    return same;
}

void testThreads(const terrain::Heightfield& field) {
    const terrain::Geography& geo = *field.geography();
    const terrain::VolcanoParams params;
    const auto start = std::chrono::steady_clock::now();
    const terrain::Volcanoes one(kSeed, geo, params, 1);
    const double oneSeconds = secondsSince(start);
    const auto again = std::chrono::steady_clock::now();
    const terrain::Volcanoes four(kSeed, geo, params, 4);
    const double fourSeconds = secondsSince(again);
    std::printf("placed in %.2f s on 1 thread, %.2f s on 4; %.2f MB\n", oneSeconds, fourSeconds,
                static_cast<double>(one.memoryBytes()) / 1.0e6);
    TEST_CHECK(sameSites(one.sites(), four.sites()));
    TEST_CHECK(sameSites(one.sites(), field.volcanoes()->sites()));
}

void testForms(const terrain::Volcanoes& volcanoes) {
    std::array<int, terrain::kVolcanoFormCount> counts{};
    int atSea = 0;
    bool consistent = true;
    for (const terrain::VolcanoSite& s : volcanoes.sites()) {
        ++counts[static_cast<std::size_t>(s.form)];
        atSea += s.atSea;
        const bool seaForm = s.form == terrain::VolcanoForm::IslandStrato ||
                             s.form == terrain::VolcanoForm::IslandShield ||
                             s.form == terrain::VolcanoForm::IslandCaldera ||
                             s.form == terrain::VolcanoForm::Seamount;
        consistent = consistent && seaForm == s.atSea;
        // The sizes: positive, the reach beyond the foot, a crater inside the foot.
        consistent = consistent && s.radius > 0.0f && s.reach > s.radius && s.elongation >= 1.0f &&
                     s.crater >= 0.0f && s.crater < s.radius && s.reach < 250'000.0f;
        if (s.atSea && s.form != terrain::VolcanoForm::Seamount) {
            consistent = consistent && s.height > 0.0f;
        }
        if (s.form == terrain::VolcanoForm::Seamount) {
            consistent = consistent && s.height < 0.0f;
        }
    }
    std::printf("%d at sea, %zu on land\n", atSea, volcanoes.sites().size() - atSea);
    for (int f = 0; f < terrain::kVolcanoFormCount; ++f) {
        std::printf("  %-16s %5d\n", terrain::volcanoFormName(static_cast<terrain::VolcanoForm>(f)),
                    counts[static_cast<std::size_t>(f)]);
        TEST_CHECK(counts[static_cast<std::size_t>(f)] > 0);
    }
    TEST_CHECK(consistent);
}

// Points drawn around the volcanoes and anywhere: every volcano whose reach holds a point is in
// the bucket of the point.
void testBuckets(const terrain::Volcanoes& volcanoes) {
    const terrain::WorldSize& w = volcanoes.world();
    const std::vector<terrain::VolcanoSite>& sites = volcanoes.sites();
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double>(state >> 11) * 0x1.0p-53;
    };
    int missed = 0;
    int held = 0;
    for (int p = 0; p < 4000; ++p) {
        const terrain::VolcanoSite& s = sites[static_cast<std::size_t>(p) % sites.size()];
        const bool around = p % 2 == 0;
        const double x = terrain::wrapCoordinate(
            around ? s.x + (next() - 0.5) * 2.2 * s.reach : (next() - 0.5) * w.x, w.x);
        const double z = terrain::wrapCoordinate(
            around ? s.z + (next() - 0.5) * 2.2 * s.reach : (next() - 0.5) * w.z, w.z);
        const auto near = volcanoes.near(x, z);
        for (std::size_t i = 0; i < sites.size(); ++i) {
            const terrain::VolcanoOffset d =
                terrain::volcanoOffset(w, sites[i].x, sites[i].z, x, z);
            if (std::hypot(d.dx, d.dz) > sites[i].reach) {
                continue;
            }
            ++held;
            bool found = false;
            for (const std::uint32_t k : near) {
                found = found || k == i;
            }
            missed += found ? 0 : 1;
        }
    }
    std::printf("buckets: %d points within the reach of a volcano, %d missed\n", held, missed);
    TEST_CHECK(held > 1000 && missed == 0);
}

// The oceanic hotspots, the heads of the chains of agent 51: the hotspots at sea, every one, an
// island shield or a seamount, its plate drifting.
void testOceanHotspots(const terrain::Volcanoes& volcanoes) {
    const std::vector<terrain::VolcanoSite> heads = volcanoes.oceanHotspots();
    int expected = 0;
    for (const terrain::VolcanoSite& s : volcanoes.sites()) {
        expected += s.origin == terrain::VolcanoOrigin::Hotspot && s.atSea ? 1 : 0;
    }
    int shields = 0;
    int seamounts = 0;
    bool drifting = true;
    for (const terrain::VolcanoSite& s : heads) {
        shields += s.form == terrain::VolcanoForm::IslandShield ? 1 : 0;
        seamounts += s.form == terrain::VolcanoForm::Seamount ? 1 : 0;
        drifting = drifting && std::hypot(s.driftX, s.driftZ) > 0.0f;
    }
    std::printf("oceanic hotspots: %zu (%d island shields, %d seamounts)\n", heads.size(), shields,
                seamounts);
    TEST_CHECK(static_cast<int>(heads.size()) == expected && expected > 20);
    TEST_CHECK(shields + seamounts == expected && drifting);
}

void testDisabled() {
    terrain::HeightfieldParams params;
    params.volcanoes.enabled = false;
    const terrain::Heightfield field(kSeed, params);
    TEST_CHECK(field.volcanoes() == nullptr);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    const terrain::Heightfield field(kSeed);
    std::printf("default Heightfield with its volcanoes in %.2f s\n", secondsSince(start));
    TEST_CHECK(field.volcanoes() != nullptr);
    if (field.volcanoes() == nullptr) {
        return test::result();
    }
    testSameAsGeology(field);
    testThreads(field);
    testForms(*field.volcanoes());
    testBuckets(*field.volcanoes());
    testOceanHotspots(*field.volcanoes());
    testDisabled();
    return test::result();
}
