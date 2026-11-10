// The biomes of the land on the real planet (agent 49), the default world with its planet file
// (the fixture coast_planet, as Unreal loads it): the share of every biome over the land and of
// the soils of the deserts. Read on the relief alone, fast (the courses of the rivers are never
// computed): its ground and slopes at 8 m, the sea of the geography, the humidity of the planet
// level, the mask of the seas of dunes; the water of the rivers, which only moves the materials
// within 150 m of them in the deserts, is left out. Every biome of the land is there.
// The table it prints is the one of the report of agent 49 (the shares of the current climate).
#include "terrain/generation/biome.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/ergs.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using terrain::Biome;
using terrain::SurfaceMaterial;

constexpr int kColumns = 420;
constexpr int kRows = 245;
constexpr double kStep = 8.0; // meters: the slopes of a column, as level 0 of the horizon

// A jitter in [0, 1) of the lattice cell (i, k), the same on every platform.
double jitter(std::uint64_t i, std::uint64_t k, std::uint64_t salt) {
    std::uint64_t z = i * 0x9E3779B97F4A7C15ULL ^ k * 0xC2B2AE3D27D4EB4FULL ^ salt;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<double>((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
}

// The site of a column of the relief: its ground and slopes at 8 m, the sea of the geography, the
// mask of the seas of dunes.
terrain::SurfaceSite siteAt(const terrain::Hydrology& h, double x, double z) {
    const terrain::Heightfield& relief = h.relief();
    const terrain::HeightSample s = relief.sampleAt(x, z);
    terrain::SurfaceSite site = terrain::heightfieldSite(
        s.height, s.continentalness,
        {relief.heightAt(x + kStep, z), relief.heightAt(x - kStep, z),
         relief.heightAt(x, z + kStep), relief.heightAt(x, z - kStep)},
        kStep);
    site.water = s.water;
    site.waterLevel = s.water == terrain::WaterKind::None ? s.height : s.waterLevel;
    const terrain::Ergs* ergs = h.ergs();
    if (ergs != nullptr && ergs->mayHold(x, z)) {
        site.erg = ergs->at(x, z, s.relief).mask;
    }
    return site;
}

} // namespace

int main() {
    const terrain::Hydrology& hydrology = *coast_planet::hydrology();
    const terrain::Ecology& ecology = coast_planet::reliefTiles().ecology();
    const terrain::EcologyParams& params = ecology.params();
    const terrain::WorldSize world = hydrology.relief().world();
    std::array<std::size_t, terrain::kBiomeCount> biomes{};
    std::array<std::size_t, terrain::kSurfaceMaterialCount> desertSoils{};
    std::size_t land = 0;
    std::size_t deserts = 0;
    for (int k = 0; k < kRows; ++k) {
        for (int i = 0; i < kColumns; ++i) {
            const auto ui = static_cast<std::uint64_t>(i);
            const auto uk = static_cast<std::uint64_t>(k);
            const double x = (i + jitter(ui, uk, 1) - kColumns / 2.0) * world.x / kColumns;
            const double z = (k + jitter(ui, uk, 2) - kRows / 2.0) * world.z / kRows;
            const terrain::SurfaceSite site = siteAt(hydrology, x, z);
            if (site.water == terrain::WaterKind::Ocean) {
                continue;
            }
            const terrain::ClimateSample climate = ecology.climate().sample(x, z, site);
            const Biome biome = terrain::classifyBiome(climate, site, params.biomes);
            ++land;
            ++biomes[static_cast<std::size_t>(biome)];
            if (terrain::isDesert(biome)) {
                ++deserts;
                ++desertSoils[static_cast<std::size_t>(
                    terrain::surfaceMaterial(biome, climate, site, params.materials))];
            }
        }
    }
    std::printf("%zu points of land on a jittered lattice of %d x %d over the planet\n", land,
                kColumns, kRows);
    bool everyBiome = true;
    bool newBiomes = true;
    for (int b = 1; b < terrain::kBiomeCount; ++b) {
        const double share = 100.0 * static_cast<double>(biomes[static_cast<std::size_t>(b)]) /
                             static_cast<double>(land);
        std::printf("  %-20s %6.2f %% of the land\n", terrain::biomeName(static_cast<Biome>(b)),
                    share);
        // The beaches and the coastal marshes are thin strips: present; every other biome on
        // 0.1 % of the land at least (agent 49: the new ones from DryForest on, and the rocky
        // desert).
        everyBiome = everyBiome && biomes[static_cast<std::size_t>(b)] >= 3;
        const bool added = b >= static_cast<int>(Biome::DryForest) || b == static_cast<int>(Biome::RockyDesert);
        newBiomes = newBiomes && (!added || share >= 0.1);
    }
    std::printf("the soils of the %zu points of the deserts:\n", deserts);
    for (int m = 0; m < terrain::kSurfaceMaterialCount; ++m) {
        const std::size_t n = desertSoils[static_cast<std::size_t>(m)];
        if (n > 0) {
            std::printf("  %-10s %6.2f %%\n",
                        terrain::materialName(static_cast<SurfaceMaterial>(m)),
                        100.0 * static_cast<double>(n) / static_cast<double>(deserts));
        }
    }
    TEST_CHECK(land > 30'000); // about 37 % of the planet
    TEST_CHECK(everyBiome);
    TEST_CHECK(newBiomes);
    return test::result();
}
