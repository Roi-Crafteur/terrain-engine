// Transitions between the biomes and the materials, on climates and sites drawn at random and
// set on every threshold: the dominant biome and material are those of the classification (the
// same bits) and their regions have a margin of exactly 0, the weights lie in [0, 1], decrease
// and sum to 1, they change continuously across every threshold, the width factor widens and
// narrows the bands, the border noise is deterministic and periodic, the compact texels keep
// the ids and sum to 255.

#include "biome_margins.hpp"

#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/surface_material.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::Biome;
using terrain::BlendParams;
using terrain::ClimateSample;
using terrain::SurfaceBlend;
using terrain::SurfaceMaterial;
using terrain::SurfaceSite;
using terrain::WaterKind;

// Deterministic draws (splitmix64), the same on every platform.
class Draws {
public:
    explicit Draws(std::uint64_t seed) : state_(seed) {}
    double next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return static_cast<double>((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
    }
    float in(double low, double high) { return static_cast<float>(low + (high - low) * next()); }
    bool chance(double p) { return next() < p; }

private:
    std::uint64_t state_;
};

struct Column {
    ClimateSample climate;
    SurfaceSite site;
};

Column drawColumn(Draws& d) {
    Column c;
    c.climate.temperature = d.in(-30.0, 35.0);
    c.climate.baseTemperature = c.climate.temperature;
    c.climate.humidity = d.in(0.0, 1.0);
    c.climate.slope = d.chance(0.5) ? d.in(0.0, 0.2) : d.in(0.0, 1.5);
    c.site.ground = d.chance(0.4) ? d.in(-20.0, 40.0) : d.in(0.0, 4000.0);
    c.site.steepness = std::max(c.climate.slope, d.in(0.0, 2.0));
    c.site.riverDistance = d.chance(0.3) ? d.in(0.0, 600.0) : terrain::kNoRiverDistance;
    c.site.cliffWall = d.chance(0.05);
    c.site.rockAboveAir = d.chance(0.05);
    c.site.water = c.site.ground < terrain::kSeaLevel ? WaterKind::Ocean
                   : d.chance(0.05)                   ? WaterKind::River
                                                      : WaterKind::None;
    c.site.waterLevel = c.site.water == WaterKind::Ocean ? terrain::kSeaLevel : c.site.ground;
    return c;
}

// Every check of one column: the dominant ids, their margins, the weights.
bool consistent(const Column& c, float factor = 1.0f) {
    const BlendParams params;
    const Biome biome = terrain::classifyBiome(c.climate, c.site);
    const SurfaceMaterial material = terrain::surfaceMaterial(biome, c.climate, c.site);
    const terrain::blend::Margins margins =
        terrain::blend::regionMargins(c.climate, c.site, {}, {}, params);
    const SurfaceBlend blend = terrain::blendSurface(c.climate, c.site, {}, {}, params, factor);
    bool ok = margins.biomes[static_cast<std::size_t>(biome)] == 0.0f &&
              margins.materials[static_cast<std::size_t>(material)] == 0.0f &&
              blend.biomes[0] == biome && blend.materials[0] == material;
    for (const auto* weights : {&blend.biomeWeights, &blend.materialWeights}) {
        const float sum = (*weights)[0] + (*weights)[1] + (*weights)[2];
        ok = ok && std::abs(sum - 1.0f) <= 1.0e-6f;
        for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
            ok = ok && (*weights)[s] >= 0.0f && (*weights)[s] <= 1.0f;
            ok = ok && (s == 0 || (*weights)[s] <= (*weights)[s - 1]);
        }
    }
    for (std::size_t s = 1; s < terrain::kBlendSlots; ++s) {
        ok = ok && (blend.biomeWeights[s] > 0.0f || blend.biomes[s] == biome);
        ok = ok && (blend.materialWeights[s] > 0.0f || blend.materials[s] == material);
        ok = ok && (blend.biomeWeights[s] == 0.0f || blend.biomes[s] != biome);
    }
    ok = ok && (blend.biomeWeights[2] == 0.0f || blend.biomes[1] != blend.biomes[2]);
    return ok;
}

void testRandomColumns() {
    Draws d(0xB1E2D);
    int failures = 0;
    for (int n = 0; n < 200'000; ++n) {
        failures += consistent(drawColumn(d), d.in(0.2, 1.8)) ? 0 : 1;
    }
    TEST_CHECK(failures == 0);
}

// The column with one input on a threshold, and one ulp to either side.
using Setter = std::function<void(Column&, float)>;

struct Threshold {
    float value;
    float width; // of its band (BlendParams)
    Setter set;
};

std::vector<Threshold> thresholds() {
    const terrain::BiomeParams b;
    const terrain::MaterialParams m;
    const BlendParams p;
    const auto relative = [&p](float threshold) { return p.relativeWidth * std::abs(threshold); };
    const Setter temperature = [](Column& c, float v) { c.climate.temperature = v; };
    const Setter humidity = [](Column& c, float v) { c.climate.humidity = v; };
    const Setter slope = [](Column& c, float v) {
        c.climate.slope = v;
        c.site.steepness = std::max(c.site.steepness, v);
    };
    const Setter steepest = [](Column& c, float v) {
        c.climate.slope = std::min(c.climate.slope, v);
        c.site.steepness = v;
    };
    const Setter altitude = [](Column& c, float v) {
        c.site.ground = terrain::kSeaLevel + v;
        c.site.water = WaterKind::None;
        c.site.waterLevel = c.site.ground;
    };
    const Setter river = [](Column& c, float v) { c.site.riverDistance = v; };
    std::vector<Threshold> list;
    for (const float t : {b.snowTemperature, b.treelineTemperature, b.marshMinTemperature,
                          b.tundraTemperature, b.taigaTemperature, b.tropicalTemperature,
                          m.mountainSnowTemperature, m.pebbleBeachTemperature}) {
        list.push_back({t, p.temperatureWidth, temperature});
    }
    for (const float h : {b.marshMinHumidity, b.taigaMinHumidity, b.temperateDesertHumidity,
                          b.forestHumidity, b.hotDesertHumidity, b.rainforestHumidity}) {
        list.push_back({h, p.humidityWidth, humidity});
    }
    for (const float s : {b.snowMaxSlope, b.mountainSlope, b.beachMaxSlope, b.marshMaxSlope}) {
        list.push_back({s, relative(s), slope});
    }
    for (const float s : {m.rockSlope, m.dirtSlope, m.screeSlope}) {
        list.push_back({s, relative(s), steepest});
    }
    for (const float a :
         {b.mountainMinAltitude, b.beachMaxHeight, b.marshMaxAltitude, b.marshCoastHeight}) {
        list.push_back({a, relative(a), altitude});
    }
    for (const float r : {b.marshRiverReach, m.riverBankWidth}) {
        list.push_back({r, relative(r), river});
    }
    return list;
}

void testOnEveryThreshold() {
    Draws d(0x7E5E);
    int failures = 0;
    for (const Threshold& t : thresholds()) {
        for (int n = 0; n < 4'000; ++n) {
            Column c = drawColumn(d);
            for (const float v :
                 {std::nextafter(t.value, -1.0e9f), t.value, std::nextafter(t.value, 1.0e9f)}) {
                t.set(c, v);
                failures += consistent(c) ? 0 : 1;
            }
        }
    }
    TEST_CHECK(failures == 0);
}

// The weight of every biome and material of a blend.
terrain::BlendWeights weightsOf(const Column& c, float factor = 1.0f) {
    terrain::BlendWeights w;
    terrain::addBlend(w, terrain::blendSurface(c.climate, c.site, {}, {}, {}, factor), 1.0f);
    return w;
}

float largestChange(const terrain::BlendWeights& a, const terrain::BlendWeights& b) {
    float change = 0.0f;
    for (std::size_t i = 0; i < a.biomes.size(); ++i) {
        change = std::max(change, std::abs(a.biomes[i] - b.biomes[i]));
    }
    for (std::size_t i = 0; i < a.materials.size(); ++i) {
        change = std::max(change, std::abs(a.materials[i] - b.materials[i]));
    }
    return change;
}

// Across each threshold, on dry land without walls, over sweeps of 3 widths on each side in
// 2,000 steps: the weights change continuously. The largest step of each sweep is cut into 64:
// a jump would keep its size in one of them, a continuous change spreads over them. The bands
// are there: the weights change by more than one quarter over many sweeps.
void testContinuousAcrossThresholds() {
    Draws d(0xC0417);
    constexpr int kSteps = 2'000;
    constexpr int kCuts = 64;
    float largestStep = 0.0f;
    float largestShare = 0.0f; // of the largest cut in its step
    int sweeps = 0;
    int banded = 0;
    for (const Threshold& t : thresholds()) {
        for (int n = 0; n < 40; ++n) {
            Column c = drawColumn(d);
            c.site.cliffWall = false;
            c.site.rockAboveAir = false;
            c.site.ground = std::max(c.site.ground, 0.5f);
            c.site.water = WaterKind::None;
            c.site.waterLevel = c.site.ground;
            const double low = t.value - 3.0 * t.width;
            const double step = 6.0 * t.width / kSteps;
            const auto at = [&](double v) {
                t.set(c, static_cast<float>(v));
                return weightsOf(c);
            };
            terrain::BlendWeights previous = at(low);
            const terrain::BlendWeights first = previous;
            float largest = 0.0f;
            int where = 1;
            for (int s = 1; s <= kSteps; ++s) {
                const terrain::BlendWeights next = at(low + s * step);
                const float change = largestChange(previous, next);
                if (change > largest) {
                    largest = change;
                    where = s;
                }
                previous = next;
            }
            float cut = 0.0f;
            terrain::BlendWeights before = at(low + (where - 1) * step);
            for (int k = 1; k <= kCuts; ++k) {
                const terrain::BlendWeights after =
                    at(low + (where - 1 + k / double{kCuts}) * step);
                cut = std::max(cut, largestChange(before, after));
                before = after;
            }
            largestStep = std::max(largestStep, largest);
            if (largest > 1.0e-3f) {
                largestShare = std::max(largestShare, cut / largest);
            }
            ++sweeps;
            banded += largestChange(first, previous) > 0.25f ? 1 : 0;
        }
    }
    std::printf("continuity: largest step %.4f over sweeps of 6 widths in %d steps, largest cut "
                "%.3f of its step, %d of %d sweeps cross a band\n",
                static_cast<double>(largestStep), kSteps, static_cast<double>(largestShare), banded,
                sweeps);
    TEST_CHECK(largestStep < 0.2f);
    TEST_CHECK(largestShare < 0.25f);
    TEST_CHECK(banded > sweeps / 4);
}

// A column one half width into grassland from temperate forest: the forest weighs about 0.2;
// a factor below 1 widens the band (more forest), above 1 narrows it (less, none from 2).
void testWidthFactor() {
    const terrain::BiomeParams biomes;
    const BlendParams params;
    Column c;
    c.climate.temperature = 12.0f;
    c.climate.humidity = biomes.forestHumidity - 0.5f * params.humidityWidth;
    c.site.ground = 300.0f;
    c.site.waterLevel = 300.0f;
    const auto forest = [&c](float factor) {
        return weightsOf(c, factor).biomes[static_cast<std::size_t>(Biome::TemperateForest)];
    };
    TEST_CHECK(forest(1.0f) > 0.1f && forest(1.0f) < 0.45f);
    TEST_CHECK(forest(0.5f) > forest(1.0f));
    TEST_CHECK(forest(1.5f) < forest(1.0f));
    TEST_CHECK(forest(2.0f) == 0.0f);
    TEST_CHECK(terrain::blendSurface(c.climate, c.site, {}, {}, params).biomes[0] ==
               Biome::Grassland);
    // On the threshold both weigh one half.
    c.climate.humidity = biomes.forestHumidity;
    const terrain::BlendWeights border = weightsOf(c);
    TEST_CHECK(std::abs(border.biomes[static_cast<std::size_t>(Biome::Grassland)] - 0.5f) < 1e-6f);
    TEST_CHECK(std::abs(border.biomes[static_cast<std::size_t>(Biome::TemperateForest)] - 0.5f) <
               1e-6f);
}

// The water, the walls and the rock above air stay sharp.
void testSharpBorders() {
    Column c;
    c.climate.temperature = 15.0f;
    c.climate.humidity = 0.5f;
    c.site.ground = -1.0f;
    c.site.water = WaterKind::Ocean;
    c.site.waterLevel = terrain::kSeaLevel;
    const SurfaceBlend ocean = terrain::blendSurface(c.climate, c.site, {}, {}, {});
    TEST_CHECK(ocean.biomes[0] == Biome::Ocean && ocean.biomeWeights[0] == 1.0f);
    TEST_CHECK(ocean.materials[0] == SurfaceMaterial::Water && ocean.materialWeights[0] == 1.0f);
    c.site.water = WaterKind::River;
    c.site.ground = 50.0f;
    c.site.waterLevel = 50.0f;
    const SurfaceBlend river = terrain::blendSurface(c.climate, c.site, {}, {}, {});
    TEST_CHECK(river.materials[0] == SurfaceMaterial::Water && river.materialWeights[0] == 1.0f);
    TEST_CHECK(river.biomes[0] != Biome::Ocean);
    c.site.water = WaterKind::None;
    c.site.cliffWall = true;
    const SurfaceBlend wall = terrain::blendSurface(c.climate, c.site, {}, {}, {});
    TEST_CHECK(wall.materials[0] == SurfaceMaterial::Rock && wall.materialWeights[0] == 1.0f);
}

void testBorderNoise() {
    const terrain::WorldSize world;
    const terrain::BlendField field(42);
    const terrain::BlendField same(42);
    const terrain::BlendField other(43);
    BlendParams flat;
    flat.noiseAmplitude = 0.0f;
    const terrain::BlendField none(42, flat);
    const BlendParams params;
    bool bounded = true;
    bool periodic = true;
    bool deterministic = true;
    bool differs = false;
    bool constant = true;
    Draws d(0x401);
    for (int n = 0; n < 2'000; ++n) {
        const double x = d.in(-3.0e7, 3.0e7);
        const double z = d.in(-1.7e7, 1.7e7);
        const float f = field.widthFactor(x, z);
        bounded = bounded && f >= 1.0f - params.noiseAmplitude && f <= 1.0f + params.noiseAmplitude;
        periodic = periodic && f == field.widthFactor(x + world.x, z - world.z);
        deterministic = deterministic && f == same.widthFactor(x, z);
        differs = differs || f != other.widthFactor(x, z);
        constant = constant && none.widthFactor(x, z) == 1.0f;
    }
    TEST_CHECK(bounded && periodic && deterministic && differs && constant);
    for (const auto& change : std::vector<std::function<void(BlendParams&)>>{
             [](BlendParams& p) { p.temperatureWidth = 0.0f; },
             [](BlendParams& p) { p.humidityWidth = -1.0f; },
             [](BlendParams& p) { p.relativeWidth = INFINITY; },
             [](BlendParams& p) { p.noiseWavelength = 0.0; },
             [](BlendParams& p) { p.noiseOctaves = 0; },
             [](BlendParams& p) { p.noiseAmplitude = 1.0f; }}) {
        BlendParams bad;
        change(bad);
        bool threw = false;
        try {
            const terrain::BlendField f(1, bad);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST_CHECK(threw);
    }
}

void testTexels() {
    Draws d(0x7E7E1);
    bool ok = true;
    for (int n = 0; n < 50'000; ++n) {
        const Column c = drawColumn(d);
        const SurfaceBlend blend =
            terrain::blendSurface(c.climate, c.site, {}, {}, {}, d.in(0.2, 1.8));
        const terrain::BlendTexel texel = terrain::packBlend(blend);
        const SurfaceBlend back = terrain::unpackBlend(texel);
        ok = ok && (texel.ids >> 24) == 0U;
        ok = ok && back.biomes[0] == blend.biomes[0] && back.materials[0] == blend.materials[0];
        for (const auto& [a, b] : {std::pair{&blend.biomeWeights, &back.biomeWeights},
                                   std::pair{&blend.materialWeights, &back.materialWeights}}) {
            int units = 0;
            for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
                const float q = (*b)[s] * 255.0f;
                units += static_cast<int>(std::lround(q));
                ok = ok && std::abs((*a)[s] - (*b)[s]) <= 1.5f / 255.0f;
                ok = ok && (s == 0 || (*b)[s] <= (*b)[s - 1]);
            }
            ok = ok && units == 255;
        }
        for (std::size_t s = 1; s < terrain::kBlendSlots; ++s) {
            ok = ok && (back.biomeWeights[s] > 0.0f ? back.biomes[s] == blend.biomes[s]
                                                    : back.biomes[s] == blend.biomes[0]);
            ok = ok && (back.materialWeights[s] > 0.0f ? back.materials[s] == blend.materials[s]
                                                       : back.materials[s] == blend.materials[0]);
        }
        terrain::BlendWeights w;
        terrain::addTexel(w, texel, 0.25f);
        float sum = 0.0f;
        for (const float v : w.biomes) {
            sum += v;
        }
        ok = ok && std::abs(sum - 0.25f) < 1e-5f;
    }
    TEST_CHECK(ok);
    // Two halves: slot 0 keeps the larger rounding.
    SurfaceBlend half;
    half.biomes = {Biome::Grassland, Biome::TemperateForest, Biome::Grassland};
    half.biomeWeights = {0.5f, 0.5f, 0.0f};
    half.materials = {SurfaceMaterial::Grass, SurfaceMaterial::Grass, SurfaceMaterial::Grass};
    half.materialWeights = {1.0f, 0.0f, 0.0f};
    const terrain::BlendTexel texel = terrain::packBlend(half);
    TEST_CHECK((texel.weights & 0xFFU) == 128U && ((texel.weights >> 8) & 0xFFU) == 127U);
    TEST_CHECK(((texel.weights >> 16) & 0xFFU) == 255U && (texel.weights >> 24) == 0U);
    TEST_CHECK((texel.ids & 0xFU) == static_cast<std::uint32_t>(Biome::Grassland));
    TEST_CHECK(((texel.ids >> 4) & 0xFU) == static_cast<std::uint32_t>(Biome::TemperateForest));
}

} // namespace

int main() {
    testRandomColumns();
    testOnEveryThreshold();
    testContinuousAcrossThresholds();
    testWidthFactor();
    testSharpBorders();
    testBorderNoise();
    testTexels();
    return test::result();
}
