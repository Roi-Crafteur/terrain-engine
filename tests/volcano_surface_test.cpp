// The ground of the volcanoes (agent 45, VolcanicGround) and what the surface makes of it. On the
// default world: every form leaves its grounds (scoria around the active craters and on the cinder
// cones, ash on the upper slopes of the cones, basalt on the shields and the walls of the
// calderas, cooled lava on the active fields, geothermal ground at the springs), none out of the
// reach of the volcanoes. The materials: basalt and cooled lava rock, scoria pebbles, ash dirt,
// geothermal ground mud where it lies at least half bare, never over the snow; the dominant of the
// blend is that material everywhere; few plants on it. In the tiles: the materials and plants of
// the bare ground of active volcanoes.
#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"
#include "volcano_on_support.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using terrain::SurfaceMaterial;
using terrain::VolcanicGround;
using terrain::VolcanoForm;

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kGrounds = terrain::kVolcanicGroundCount;

// The grounds lying at least half bare over the reach of the first volcanoes of each form, and
// at the springs.
struct Grounds {
    std::array<std::array<int, kGrounds>, terrain::kVolcanoFormCount> byForm{};
    int springs = 0;
    int geothermal = 0;
};

void countVolcano(const terrain::Heightfield& field, const terrain::VolcanoSite& v,
                  Grounds& out) {
    const terrain::VolcanoOffset top = terrain::volcanoSummit(v);
    auto& row = out.byForm[static_cast<std::size_t>(v.form)];
    std::vector<double> xs;
    std::vector<double> zs;
    for (int k = 0; k < 2000; ++k) {
        const double r = 0.9 * static_cast<double>(v.reach) * std::sqrt((k + 0.5) / 2000.0);
        const double a = 2.39996323 * k;
        xs.push_back(v.x + top.dx + r * std::cos(a));
        zs.push_back(v.z + top.dz + r * std::sin(a));
    }
    std::vector<terrain::HeightSample> s(xs.size());
    field.samplesAt(xs, zs, s);
    for (const terrain::HeightSample& h : s) {
        if (h.volcanicShare >= 128) {
            ++row[static_cast<std::size_t>(h.volcanicGround)];
        }
    }
}

void testGrounds(const terrain::Heightfield& field) {
    Grounds g;
    std::array<int, terrain::kVolcanoFormCount> seen{};
    for (const terrain::VolcanoSite& v : field.volcanoes()->sites()) {
        int& n = seen[static_cast<std::size_t>(v.form)];
        if (n < 4 && v.form != VolcanoForm::Seamount && (n < 2 || v.active)) {
            ++n;
            countVolcano(field, v, g);
        }
    }
    const terrain::WorldSize& w = field.world();
    const std::vector<terrain::GeothermalSite> springs =
        field.geothermalIn(-0.5 * w.x, -0.5 * w.z, 0.5 * w.x, 0.5 * w.z);
    for (std::size_t i = 0; i < springs.size(); i += 7) {
        const terrain::HeightSample h = field.sampleAt(springs[i].x, springs[i].z);
        ++g.springs;
        g.geothermal += h.volcanicGround == VolcanicGround::Geothermal && h.volcanicShare >= 230;
    }
    for (int f = 0; f < terrain::kVolcanoFormCount; ++f) {
        const auto& row = g.byForm[static_cast<std::size_t>(f)];
        std::printf("%-16s", terrain::volcanoFormName(static_cast<VolcanoForm>(f)));
        for (std::size_t k = 1; k < kGrounds; ++k) {
            std::printf(" %s %4d", terrain::volcanicGroundName(static_cast<VolcanicGround>(k)),
                        row[k]);
        }
        std::printf("\n");
    }
    std::printf("%d springs: %d on bare geothermal ground\n", g.springs, g.geothermal);
    const auto count = [&](VolcanoForm f, VolcanicGround k) {
        return g.byForm[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)];
    };
    TEST_CHECK(count(VolcanoForm::Strato, VolcanicGround::Ash) > 20);
    TEST_CHECK(count(VolcanoForm::Strato, VolcanicGround::Scoria) > 0);
    TEST_CHECK(count(VolcanoForm::Shield, VolcanicGround::Basalt) > 20);
    TEST_CHECK(count(VolcanoForm::CinderField, VolcanicGround::Scoria) > 0);
    TEST_CHECK(count(VolcanoForm::LavaField, VolcanicGround::CooledLava) > 20);
    TEST_CHECK(count(VolcanoForm::Caldera, VolcanicGround::Basalt) > 0);
    TEST_CHECK(count(VolcanoForm::MaarField, VolcanicGround::Ash) > 0);
    TEST_CHECK(g.springs > 50 && g.geothermal == g.springs);
}

terrain::ClimateSample climate(float temperature, float humidity) {
    terrain::ClimateSample c;
    c.baseTemperature = temperature;
    c.temperature = temperature;
    c.humidity = humidity;
    return c;
}

terrain::SurfaceSite volcanicSite(VolcanicGround ground, float share, float altitude) {
    terrain::SurfaceSite s;
    s.ground = altitude;
    s.waterLevel = altitude;
    s.volcanicGround = ground;
    s.volcanicShare = share;
    return s;
}

// The material, out of the snow, then the dominant of the blend everywhere.
void testMaterials() {
    const terrain::BiomeParams biomes;
    const terrain::MaterialParams materials;
    const terrain::BlendParams blend;
    const auto material = [&](const terrain::ClimateSample& c, const terrain::SurfaceSite& s) {
        return terrain::surfaceMaterial(terrain::classifyBiome(c, s, biomes), c, s, materials);
    };
    const terrain::ClimateSample mild = climate(12.0f, 0.35f);
    const terrain::ClimateSample hot = climate(26.0f, 0.05f);
    const terrain::ClimateSample cold = climate(-12.0f, 0.4f);
    TEST_CHECK(material(mild, volcanicSite(VolcanicGround::Basalt, 0.9f, 300.0f)) ==
               SurfaceMaterial::Rock);
    TEST_CHECK(material(mild, volcanicSite(VolcanicGround::CooledLava, 0.9f, 300.0f)) ==
               SurfaceMaterial::Rock);
    TEST_CHECK(material(mild, volcanicSite(VolcanicGround::Scoria, 0.9f, 300.0f)) ==
               SurfaceMaterial::Pebbles);
    TEST_CHECK(material(mild, volcanicSite(VolcanicGround::Ash, 0.9f, 300.0f)) ==
               SurfaceMaterial::Dirt);
    TEST_CHECK(material(mild, volcanicSite(VolcanicGround::Geothermal, 0.9f, 300.0f)) ==
               SurfaceMaterial::Mud);
    TEST_CHECK(material(mild, volcanicSite(VolcanicGround::Basalt, 0.3f, 300.0f)) ==
               SurfaceMaterial::Grass);
    TEST_CHECK(material(hot, volcanicSite(VolcanicGround::CooledLava, 0.9f, 300.0f)) ==
               SurfaceMaterial::Rock);
    TEST_CHECK(material(cold, volcanicSite(VolcanicGround::Ash, 1.0f, 3500.0f)) ==
               SurfaceMaterial::Snow);
    std::size_t mismatches = 0;
    std::size_t blends = 0;
    for (const terrain::ClimateSample& c : {mild, hot, cold, climate(-3.0f, 0.3f)}) {
        for (int k = 0; k < terrain::kVolcanicGroundCount; ++k) {
            for (int share = 0; share <= 40; ++share) {
                for (const float altitude : {2.0f, 300.0f, 2500.0f}) {
                    for (const float factor : {0.2f, 1.0f, 1.8f}) {
                        const terrain::SurfaceSite s = volcanicSite(
                            static_cast<VolcanicGround>(k), 0.025f * static_cast<float>(share),
                            altitude);
                        const terrain::SurfaceBlend b =
                            terrain::blendSurface(c, s, biomes, materials, blend, factor);
                        mismatches += b.materials[0] == material(c, s) ? 0 : 1;
                        ++blends;
                    }
                }
            }
        }
    }
    std::printf("%zu blends of volcanic grounds: %zu whose dominant is not surfaceMaterial\n",
                blends, mismatches);
    TEST_CHECK(mismatches == 0);
}

// Few plants on the bare ground of the volcanoes: none where it lies all bare.
void testPlants() {
    const terrain::VegetationField plants(terrain::kDefaultWorldSeed);
    const terrain::ClimateSample c = climate(18.0f, 0.6f);
    const terrain::BiomeParams biomes;
    const auto total = [&](float share) {
        const terrain::SurfaceSite s = volcanicSite(VolcanicGround::Ash, share, 300.0f);
        const terrain::Biome biome = terrain::classifyBiome(c, s, biomes);
        const terrain::PlantDensities d =
            plants.densities(10.0, 20.0, biome, SurfaceMaterial::Grass, c, s);
        float sum = 0.0f;
        for (const float v : d) {
            sum += v;
        }
        return sum;
    };
    std::printf("plants on grass: %.3f, on ground of the volcanoes 40 %% bare %.3f, all bare "
                "%.3f\n",
                static_cast<double>(total(0.0f)), static_cast<double>(total(0.4f)),
                static_cast<double>(total(1.0f)));
    TEST_CHECK(total(0.0f) > 0.0f && total(0.4f) < total(0.0f) && total(1.0f) == 0.0f);
}

// The tiles of LOD 0 on the bare ground of active lava fields and cinder fields: their blends show
// rock, pebbles or mud.
void testTiles() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, volcano_test::on(), {}, std::nullopt, {},
                                      nullptr, true, 0);
    int volcanic = 0;
    int texels = 0;
    int tilesSeen = 0;
    for (const terrain::VolcanoSite& v : tiles.heightfield().volcanoes()->sites()) {
        if (!v.active || (v.form != VolcanoForm::LavaField && v.form != VolcanoForm::CinderField) ||
            v.atSea || tilesSeen >= 6) {
            continue;
        }
        // The first point of a spiral from its center where its ground lies all but bare.
        double x = v.x;
        double z = v.z;
        for (int k = 0; k < 4000; ++k) {
            const double r = static_cast<double>(v.radius) * std::sqrt((k + 0.5) / 4000.0);
            x = v.x + r * std::cos(2.39996323 * k);
            z = v.z + r * std::sin(2.39996323 * k);
            if (tiles.heightfield().sampleAt(x, z).volcanicShare >= 240) {
                break;
            }
        }
        ++tilesSeen;
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
        const terrain::TileData tile = tiles.generate({terrain::tileContaining(chunk, 0), 0});
        for (const terrain::BlendTexel& t : tile.surfaceBlend) {
            const SurfaceMaterial m = terrain::unpackBlend(t).materials[0];
            volcanic += m == SurfaceMaterial::Rock || m == SurfaceMaterial::Pebbles ||
                                m == SurfaceMaterial::Mud
                            ? 1
                            : 0;
            ++texels;
        }
    }
    std::printf("%d tiles of active fields: %d of %d texels rock, pebbles or mud\n", tilesSeen,
                volcanic, texels);
    TEST_CHECK(tilesSeen >= 3 && volcanic * 2 > texels);
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, volcano_test::on());
    testGrounds(field);
    testMaterials();
    testPlants();
    testTiles();
    return test::result();
}
