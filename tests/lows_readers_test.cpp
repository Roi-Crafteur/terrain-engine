// The readers that told the sea from the height read the water instead (agent 52): dry land below
// the sea level is never the sea. On the default world, at the lowest points of its dry
// depressions below the sea level (phase 11B):
// - Heightfield::mayBeDryBelowSea is true there, false on the sea by the origin;
// - the tiles of LOD 0 keep the plants of their biome below the sea level, and never put a land
//   plant under the sea (the tiles of the coast at the origin);
// - the far horizon raises the buttes of their floors to their crests below the sea level, and
//   still never raises a sample of the sea;
// - the caves are carved under them (no deep sea there), as they are not under the deep sea;
// - dry ground below the sea level warms by the lapse rate (ClimateParams::dryBelowSeaWarming):
//   the same code with it on and off changes the ecology of no column of the sea, of a lake or
//   above the sea level (random columns over the world), only dry columns below it.
#include "terrain/generation/caves.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_crests.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::HeightSample;
using terrain::Heightfield;
using terrain::WaterKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

const Heightfield& field() {
    static const Heightfield f(kSeed);
    return f;
}

struct Floor {
    double x = 0.0;
    double z = 0.0;
    float ground = 0.0f;
};

// The lowest dry point of each depression on a grid of 33 x 33 over its radius.
const std::vector<Floor>& floors() {
    static const std::vector<Floor> out = [] {
        std::vector<Floor> f;
        for (const terrain::Depression& d : field().depressions()) {
            Floor best{d.x, d.z, 1.0e9f};
            for (int j = -16; j <= 16; ++j) {
                for (int i = -16; i <= 16; ++i) {
                    const double x = d.x + i * d.radius / 16.0;
                    const double z = d.z + j * d.radius / 16.0;
                    const HeightSample s = field().sampleAt(x, z);
                    if (s.water == WaterKind::None && s.height < best.ground) {
                        best = {x, z, s.height};
                    }
                }
            }
            if (best.ground < -50.0f) {
                f.push_back(best);
            }
        }
        return f;
    }();
    return out;
}

void testMayBeDry() {
    std::printf("%zu dry floors below -50 m\n", floors().size());
    TEST_CHECK(floors().size() >= 10);
    for (const Floor& f : floors()) {
        TEST_CHECK(field().mayBeDryBelowSea(f.x, f.z));
        TEST_CHECK(field().mayBeDryBelowSea(f.x + field().world().x, f.z)); // any image
    }
    TEST_CHECK(field().sampleAt(1234.5, -6789.25).water == WaterKind::Ocean);
    TEST_CHECK(!field().mayBeDryBelowSea(1234.5, -6789.25));
}

// Plants of the tiles of LOD 0 over the floors: some below the sea level; none of a land type
// under the sea on the coast of the origin.
void testPlants() {
    const terrain::TerrainTiles tiles(kSeed);
    std::size_t below = 0;
    for (const Floor& f : floors()) {
        const terrain::TileKey key = terrain::tileContaining(terrain::worldToChunk(f.x, f.z), 0);
        for (const terrain::Plant& p : tiles.generate({key, 0, 0}).plants) {
            below += p.y < terrain::kSeaLevel ? 1 : 0;
        }
    }
    std::printf("%zu plants below the sea level on the dry floors\n", below);
    TEST_CHECK(below > 0);
    std::size_t wet = 0;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            const terrain::TileKey key{{dx * 4, dz * 4}, 0};
            for (const terrain::Plant& p : tiles.generate({key, 0, 0}).plants) {
                const bool sea = field().sampleAt(p.x, p.z).water == WaterKind::Ocean;
                wet += sea && p.type != terrain::PlantType::Seaweed ? 1 : 0;
            }
        }
    }
    TEST_CHECK(wet == 0);
}

// The lifts of the samples below the sea level of a window of level 3 around (x, z).
struct Lifts {
    std::size_t below = 0;  // samples below the sea level
    std::size_t raised = 0; // of them, raised
};
Lifts liftsAround(const terrain::HorizonCrestSampler& crests, double x, double z) {
    constexpr int kLevel = 3;
    const int pitch = terrain::levelPitch(crests.params(), kLevel);
    const auto i = static_cast<std::int64_t>(std::floor(x / pitch));
    const auto k = static_cast<std::int64_t>(std::floor(z / pitch));
    const terrain::HorizonRect rect{i - 40, k - 40, i + 40, k + 40};
    const std::vector<float> lifts = crests.sample(kLevel, rect);
    Lifts out;
    std::size_t n = 0;
    for (std::int64_t b = rect.z0; b < rect.z1; ++b) {
        for (std::int64_t a = rect.x0; a < rect.x1; ++a, ++n) {
            if (field().heightAt(terrain::samplePosition(a, pitch),
                                 terrain::samplePosition(b, pitch)) < terrain::kSeaLevel) {
                ++out.below;
                out.raised += lifts[n] > 0.0f ? 1 : 0;
            }
        }
    }
    return out;
}

void testCrests() {
    terrain::HorizonParams params;
    params.world = field().world();
    const terrain::HorizonCrestSampler crests(field(), params);
    Lifts dry;
    for (std::size_t k = 0; k < floors().size() && k < 6; ++k) {
        const Lifts l = liftsAround(crests, floors()[k].x, floors()[k].z);
        dry.below += l.below;
        dry.raised += l.raised;
    }
    std::printf("level 3, dry floors: %zu of %zu samples below the sea level raised\n",
                dry.raised, dry.below);
    TEST_CHECK(dry.below > 1000 && dry.raised > 0);
    const Lifts sea = liftsAround(crests, 1234.5, -6789.25);
    TEST_CHECK(sea.below > 100 && sea.raised == 0);
}

// The exclusion of the caves under the deepest floor: none without the sea (dry low), the deep
// sea without the predicate (the finite regions).
void testCaves() {
    const terrain::CaveField caves(kSeed, terrain::CaveParams{}, field().world());
    const Floor deepest = *std::min_element(
        floors().begin(), floors().end(),
        [](const Floor& a, const Floor& b) { return a.ground < b.ground; });
    std::printf("the deepest dry floor: %.1f m\n", static_cast<double>(deepest.ground));
    terrain::CaveGround ground;
    ground.height = [](double x, double z) { return field().heightAt(x, z); };
    const terrain::CaveColumn old = caves.column(deepest.x, deepest.z, deepest.ground, ground, {});
    ground.dryLow = [](double x, double z) {
        return field().mayBeDryBelowSea(x, z) && field().sampleAt(x, z).water != WaterKind::Ocean;
    };
    const terrain::CaveColumn dry = caves.column(deepest.x, deepest.z, deepest.ground, ground, {});
    TEST_CHECK(dry.exclusion < 0.0f);
    TEST_CHECK(deepest.ground > -200.0f || old.exclusion > 0.0f);
}

// The column (x, z) as the biomes read it, its true water.
terrain::SurfaceSite siteAt(double x, double z) {
    constexpr double kStep = 8.0;
    const HeightSample s = field().sampleAt(x, z);
    terrain::SurfaceSite site;
    site.ground = s.height;
    site.continentalness = s.continentalness;
    const float east = field().heightAt(x + kStep, z);
    const float west = field().heightAt(x - kStep, z);
    const float south = field().heightAt(x, z + kStep);
    const float north = field().heightAt(x, z - kStep);
    site.gradX = (east - west) / static_cast<float>(2.0 * kStep);
    site.gradZ = (south - north) / static_cast<float>(2.0 * kStep);
    site.water = s.water;
    site.waterLevel = s.waterLevel;
    return site;
}

bool sameEcology(const terrain::EcologySample& a, const terrain::EcologySample& b) {
    return bit_exact::same(a.climate.temperature, b.climate.temperature) && a.biome == b.biome &&
           a.material == b.material;
}

// The ecology with the warming of dry ground below the sea level on and off.
void testClimate() {
    terrain::EcologyParams off;
    off.climate.dryBelowSeaWarming = false;
    const terrain::Ecology warm(kSeed, terrain::EcologyParams{}, field().world());
    const terrain::Ecology cold(kSeed, off, field().world());
    const Floor& deepest = *std::min_element(
        floors().begin(), floors().end(),
        [](const Floor& a, const Floor& b) { return a.ground < b.ground; });
    const terrain::SurfaceSite floor = siteAt(deepest.x, deepest.z);
    const float gain = warm.sampleAt(deepest.x, deepest.z, floor).climate.temperature -
                       cold.sampleAt(deepest.x, deepest.z, floor).climate.temperature;
    const float expected = terrain::ClimateParams{}.lapseRate * -floor.ground;
    std::printf("the deepest dry floor (%.1f m) warmer by %.2f C\n",
                static_cast<double>(floor.ground), static_cast<double>(gain));
    TEST_CHECK(std::abs(gain - expected) < 0.01f);
    std::size_t changed = 0;
    std::size_t elsewhere = 0;
    std::size_t sea = 0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 3e7)) {
        const terrain::SurfaceSite site = siteAt(p.x, p.z);
        sea += site.water == WaterKind::Ocean ? 1 : 0;
        if (sameEcology(warm.sampleAt(p.x, p.z, site), cold.sampleAt(p.x, p.z, site))) {
            continue;
        }
        ++changed;
        elsewhere += site.dry() && site.ground < terrain::kSeaLevel ? 0 : 1;
    }
    for (const Floor& f : floors()) {
        const terrain::SurfaceSite site = siteAt(f.x, f.z);
        changed += sameEcology(warm.sampleAt(f.x, f.z, site), cold.sampleAt(f.x, f.z, site)) ? 0 : 1;
    }
    std::printf("%zu columns changed (%zu of the sea among the random ones), %zu elsewhere than "
                "on dry ground below the sea level\n",
                changed, sea, elsewhere);
    TEST_CHECK(changed >= floors().size() && elsewhere == 0 && sea > 5'000);
}

} // namespace

int main() {
    testMayBeDry();
    testPlants();
    testCrests();
    testCaves();
    testClimate();
    return test::result();
}
