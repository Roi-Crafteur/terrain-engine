// The floors of the seas of dunes between their dunes (agent 34, tenth stop: the corridors).
// - Kinds: over ergs of the three characters of the default world, the floor between the dunes
//   is sand, gravel or crust by place (Ergs::dunesAt, floor; read where the mask is full and the
//   dunes under 10 m deep): each kind on 1 % of them at least (the planet of the game: sand 7 to
//   94 %, gravel 6 to 81 %, crust 0 to 22 % from one erg to the next), none on more than 90 %,
//   and the kind changes slowly (50 m apart, by 0.25 at most: patches, no speckle).
// - Materials: on a grassland, the floor of an erg away from the water is sand, gravel or crust
//   by its kind (agent 49: pebbles and dirt before), never grass; within ergGrassReach of a river and off
//   the erg, grass. The dominant material of the blend is surfaceMaterial everywhere; the sand of
//   the dunes thins out over 3 m of sand on the floor of an erg (its weight 0.5 at 1.5 m), never
//   on the ground off the ergs, however wide the noise of the borders makes the bands.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/surface_material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using dunes_test::real;
using terrain::ErgCharacter;
using terrain::SurfaceMaterial;

// Sites of distinct ergs of a character (their mask full), at most count.
std::vector<std::array<double, 2>> sitesOf(ErgCharacter c, std::size_t count) {
    std::vector<std::array<double, 2>> out;
    for (const terrain::ErgSummary& erg : real().ergs.ergs()) {
        if (erg.character != c || out.size() >= count) {
            continue;
        }
        for (int j = -10; j <= 10; ++j) {
            bool found = false;
            for (int i = -10; i <= 10 && !found; ++i) {
                const double x = erg.x + 65'536.0 * i;
                const double z = erg.z + 65'536.0 * j;
                if (real().ergs.mayHold(x, z) && real().ergs.at(x, z, 0.0f).mask > 0.99f) {
                    out.push_back({x, z});
                    found = true;
                }
            }
            if (found) {
                break;
            }
        }
    }
    return out;
}

void testKinds() {
    std::array<std::size_t, 3> kinds{};
    std::size_t floors = 0;
    std::size_t all = 0;
    double jump = 0.0;
    for (const ErgCharacter c :
         {ErgCharacter::Realistic, ErgCharacter::Waves, ErgCharacter::Giants}) {
        for (const std::array<double, 2>& site : sitesOf(c, 3)) {
            for (int k = -60; k < 60; ++k) {
                float last = -1.0f;
                for (int i = -60; i < 60; ++i) {
                    const double x = site[0] + 50.0 * i;
                    const double z = site[1] + 50.0 * k;
                    const terrain::HeightSample s; // a flat relief: the dunes alone
                    float mask = 0.0f;
                    float floor = 0.0f;
                    const float dunes = real().ergs.dunesAt(x, z, s, 0.0, &mask, nullptr, &floor);
                    // The kind is read where the dunes are under 10 m deep (dunesAt).
                    const bool read = mask > 0.99f && dunes < 10.0f;
                    if (last >= 0.0f && read) {
                        jump = std::max(jump, static_cast<double>(std::abs(floor - last)));
                    }
                    last = read ? floor : -1.0f;
                    if (read) {
                        ++kinds[floor < 0.5f ? 0 : (floor <= 1.5f ? 1 : 2)];
                        ++all;
                        floors += dunes < 1.5f ? 1 : 0;
                    }
                }
            }
        }
    }
    const auto share = [&](std::size_t k) {
        return all > 0 ? static_cast<double>(kinds[k]) / static_cast<double>(all) : 0.0;
    };
    std::printf("floors of the ergs: %zu samples (%zu between their dunes), sand %.0f %%, gravel "
                "%.0f %%, crust %.0f %%; the kind changes by %.3f at most 50 m apart\n",
                all, floors, 100.0 * share(0), 100.0 * share(1), 100.0 * share(2), jump);
    TEST_CHECK(all >= 40'000 && floors >= 2'000);
    for (std::size_t k = 0; k < 3; ++k) {
        TEST_CHECK(share(k) >= 0.01 && share(k) <= 0.9);
    }
    TEST_CHECK(jump <= 0.25);
}

// A grassland site: the climate and the ground of a temperate plain.
terrain::ClimateSample grassland() {
    terrain::ClimateSample c;
    c.baseTemperature = 12.0f;
    c.temperature = 12.0f;
    c.humidity = 0.35f;
    return c;
}

terrain::SurfaceSite floorSite(float dunes, float erg, float kind, float river) {
    terrain::SurfaceSite s;
    s.ground = 120.0f;
    s.waterLevel = s.ground;
    s.dunes = dunes;
    s.erg = erg;
    s.ergFloor = kind;
    s.riverDistance = river;
    return s;
}

float weightOf(const terrain::SurfaceBlend& b, SurfaceMaterial m) {
    float w = 0.0f;
    for (int k = 0; k < terrain::kBlendSlots; ++k) {
        const auto slot = static_cast<std::size_t>(k);
        w += b.materials[slot] == m ? b.materialWeights[slot] : 0.0f;
    }
    return w;
}

void testMaterials() {
    const terrain::ClimateSample c = grassland();
    const terrain::BiomeParams biomes;
    const terrain::MaterialParams materials;
    const terrain::BlendParams blend;
    TEST_CHECK(terrain::classifyBiome(c, floorSite(0.0f, 0.0f, 0.0f, 1.0e4f), biomes) ==
               terrain::Biome::Grassland);
    const auto material = [&](const terrain::SurfaceSite& s) {
        return terrain::surfaceMaterial(terrain::classifyBiome(c, s, biomes), c, s, materials);
    };
    TEST_CHECK(material(floorSite(0.0f, 1.0f, 0.0f, 1.0e4f)) == SurfaceMaterial::Sand);
    TEST_CHECK(material(floorSite(0.0f, 1.0f, 1.0f, 1.0e4f)) == SurfaceMaterial::Gravel);
    TEST_CHECK(material(floorSite(0.0f, 1.0f, 2.0f, 1.0e4f)) == SurfaceMaterial::Crust);
    TEST_CHECK(material(floorSite(0.0f, 1.0f, 1.0f, 50.0f)) == SurfaceMaterial::Grass);
    TEST_CHECK(material(floorSite(0.0f, 0.0f, 1.0f, 1.0e4f)) == SurfaceMaterial::Grass);
    TEST_CHECK(material(floorSite(3.0f, 1.0f, 1.0f, 1.0e4f)) == SurfaceMaterial::Sand);
    // The dominant of the blend everywhere, no grass on the floor of a full erg away from water.
    std::size_t mismatches = 0;
    float grass = 0.0f;
    for (int d = -20; d <= 40; ++d) {
        for (int k = 0; k <= 20; ++k) {
            for (const float erg : {0.0f, 0.3f, 0.5f, 0.7f, 1.0f}) {
                for (const float river : {0.0f, 100.0f, 150.0f, 200.0f, 1.0e4f}) {
                    for (const float factor : {0.2f, 1.0f, 1.8f}) {
                        const terrain::SurfaceSite s = floorSite(
                            0.1f * static_cast<float>(d), erg, 0.1f * static_cast<float>(k), river);
                        const terrain::SurfaceBlend b =
                            terrain::blendSurface(c, s, biomes, materials, blend, factor);
                        mismatches += b.materials[0] == material(s) ? 0 : 1;
                        if (erg == 1.0f && river == 1.0e4f) {
                            grass = std::max(grass, weightOf(b, SurfaceMaterial::Grass));
                        }
                    }
                }
            }
        }
    }
    // The sand of the dunes over the floor (gravel), and off the ergs.
    const auto sand = [&](float dunes, float erg, float factor) {
        return weightOf(terrain::blendSurface(c, floorSite(dunes, erg, 1.0f, 1.0e4f), biomes,
                                              materials, blend, factor),
                        SurfaceMaterial::Sand);
    };
    const auto at = [&](float dunes) { return static_cast<double>(sand(dunes, 1.0f, 1.0f)); };
    std::printf("floor materials: %zu blends whose dominant is not surfaceMaterial, grass %.3f at "
                "most on the floor of an erg; the sand of the dunes weighs %.2f, %.2f, %.2f, %.2f "
                "at 0, 1.5, 2 and 3 m on gravel, %.2f off the ergs at 0 m (bands 5 times wider: "
                "the desert of the biome)\n",
                mismatches, static_cast<double>(grass), at(0.0f), at(1.5f), at(2.0f), at(3.0f),
                static_cast<double>(sand(0.0f, 0.0f, 0.2f)));
    TEST_CHECK(mismatches == 0);
    TEST_CHECK(grass == 0.0f);
    TEST_CHECK(sand(0.0f, 1.0f, 1.0f) == 0.0f);
    TEST_CHECK(std::abs(sand(1.5f, 1.0f, 1.0f) - 0.5f) <= 0.05f);
    TEST_CHECK(sand(2.0f, 1.0f, 1.0f) > 0.5f && sand(2.0f, 1.0f, 1.0f) < 1.0f);
    TEST_CHECK(sand(3.0f, 1.0f, 1.0f) == 1.0f);
    TEST_CHECK(sand(0.0f, 0.0f, 0.2f) == sand(-5.0f, 0.0f, 0.2f)); // the desert of the biome alone
}

} // namespace

int main() {
    testMaterials();
    testKinds();
    return test::result();
}
