// Land below the sea level in a sea of dunes is dry sand (agent 34, ergs.hpp; the water of a column
// is the marker of the geography and the hydrology, never its height):
// - Columns: the floor and the dunes of an erg, dry (no water), from 300 m under the sea level
//   to 10 m over it, in the climate of a hot desert: their biome sand desert (never ocean, beach or
//   marsh), their dominant material sand and their blend all of the desert, whatever the width of
//   its bands.
// - The dry depressions of the default world: a sea of dunes covers their floor under the sea
//   level (dry land, never sea), as the depressions keep off the dunes only where they hold water.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/surface_material.hpp"

#include <cstdio>

namespace {

using terrain::Biome;
using terrain::SurfaceMaterial;
using dunes_test::real;

terrain::ClimateSample hotDesert() {
    terrain::ClimateSample c;
    c.baseTemperature = 26.0f;
    c.temperature = 26.0f;
    c.humidity = 0.05f;
    return c;
}

terrain::SurfaceSite ergColumn(float ground, float dunes) {
    terrain::SurfaceSite s;
    s.ground = ground;
    s.waterLevel = ground;
    s.water = terrain::WaterKind::None;
    s.dunes = dunes;
    s.erg = 1.0f;
    return s;
}

void testColumns() {
    const terrain::ClimateSample c = hotDesert();
    const terrain::BiomeParams biomes;
    const terrain::MaterialParams materials;
    const terrain::BlendParams blend;
    std::size_t columns = 0;
    std::size_t wrongBiome = 0;
    std::size_t wrongMaterial = 0;
    std::size_t wetBlend = 0;
    for (int g = -300; g <= 10; ++g) {
        for (const float dunes : {0.0f, 2.0f, 30.0f}) {
            const terrain::SurfaceSite s = ergColumn(static_cast<float>(g), dunes);
            const Biome b = terrain::classifyBiome(c, s, biomes);
            ++columns;
            wrongBiome += b == Biome::SandDesert ? 0 : 1; // a sea of dunes (agent 49)
            wrongMaterial +=
                terrain::surfaceMaterial(b, c, s, materials) == SurfaceMaterial::Sand ? 0 : 1;
            for (const float factor : {0.2f, 1.0f, 1.8f}) {
                const terrain::SurfaceBlend sb =
                    terrain::blendSurface(c, s, biomes, materials, blend, factor);
                for (std::size_t k = 0; k < terrain::kBlendSlots; ++k) {
                    const bool wet = sb.biomes[k] == Biome::Ocean || sb.biomes[k] == Biome::Beach ||
                                     sb.biomes[k] == Biome::Marsh;
                    wetBlend += wet && sb.biomeWeights[k] > 0.0f ? 1 : 0;
                }
            }
        }
    }
    std::printf("dry columns of an erg from -300 to 10 m: %zu, %zu not desert, %zu not sand, %zu "
                "blends with ocean, beach or marsh\n",
                columns, wrongBiome, wrongMaterial, wetBlend);
    TEST_CHECK(wrongBiome == 0 && wrongMaterial == 0 && wetBlend == 0);
}

// The dry depressions below the sea level of the default world (coarse planet), the ergs built
// with them: where a sea of dunes meets one, it covers its floor under the sea level, dry land
// (the geography marks the sea, never the height), and keeps off none of it.
void testDryDepressions() {
    const terrain::Ergs ergs{dunes_test::kSeed, *real().planet, real().relief.depressions(),
                             terrain::ErgParams{}};
    std::size_t below = 0;
    std::size_t covered = 0;
    std::size_t sea = 0;
    std::size_t met = 0;
    for (const terrain::Depression& d : real().relief.depressions()) {
        std::size_t here = 0;
        for (double v = -d.radius; v <= d.radius; v += 2048.0) {
            for (double u = -d.radius; u <= d.radius; u += 2048.0) {
                const double x = d.x + u;
                const double z = d.z + v;
                if (!ergs.mayHold(x, z)) {
                    continue;
                }
                const terrain::HeightSample s = real().relief.sampleAt(x, z);
                if (!(s.height < terrain::kSeaLevel)) {
                    continue;
                }
                ++below;
                sea += s.water == terrain::WaterKind::Ocean ? 1 : 0;
                const bool erg = ergs.at(x, z, s.relief).mask > 0.5f;
                covered += erg ? 1 : 0;
                here += erg ? 1 : 0;
            }
        }
        met += here > 0 ? 1 : 0;
    }
    std::printf("dry depressions: %zu points under the sea level where ergs may lie, %zu of them "
                "sea, %zu in a sea of dunes (in %zu depressions)\n",
                below, sea, covered, met);
    TEST_CHECK(sea == 0);
    TEST_CHECK(met >= 1 && covered >= 20);
}

} // namespace

int main() {
    testColumns();
    testDryDepressions();
    return test::result();
}
