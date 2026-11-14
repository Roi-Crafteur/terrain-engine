#include "biome_margins.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::blend {

namespace {

constexpr float kRuledOut = std::numeric_limits<float>::infinity();
// Narrowest width of a band given as a share of a threshold of 0.
constexpr float kMinWidth = 1.0e-3f;

// Margins of a condition and of its negation, in widths: 0 where it holds (where it fails).
struct Test {
    float holds = 0.0f;
    float fails = 0.0f;
};

// value <= threshold or value < threshold: the same margins, both 0 on the threshold. The
// difference of two floats is exact in double, so its sign is that of the comparison.
Test atMost(float value, float threshold, float width) {
    const double d = (static_cast<double>(value) - threshold) / width;
    return {d > 0.0 ? static_cast<float>(d) : 0.0f, d < 0.0 ? static_cast<float>(-d) : 0.0f};
}

Test negate(Test t) {
    return {t.fails, t.holds};
}

Test atLeast(float value, float threshold, float width) {
    return negate(atMost(value, threshold, width));
}

Test both(Test a, Test b) {
    return {std::max(a.holds, b.holds), std::min(a.fails, b.fails)};
}

Test either(Test a, Test b) {
    return {std::min(a.holds, b.holds), std::max(a.fails, b.fails)};
}

Test known(bool value) {
    return value ? Test{0.0f, kRuledOut} : Test{kRuledOut, 0.0f};
}

// What the decisions read from a column, and the widths of their bands.
struct Column {
    float temperature;
    float humidity;
    float slope;    // |grad H|, for the biomes
    float steepest; // steepestSlope, for the materials
    float altitude;
    float riverDistance;
    bool wall;
    bool rockAboveAir;
    float temperatureWidth;
    float humidityWidth;
    float relativeWidth;
    float dunes; // meters of sand of the seas of dunes (SurfaceSite::dunes)
    float erg;   // SurfaceSite::erg
    float ergFloor; // SurfaceSite::ergFloor
    float volcanicShare;           // SurfaceSite::volcanicShare
    VolcanicGround volcanicGround; // SurfaceSite::volcanicGround

    [[nodiscard]] float widthOf(float threshold) const {
        return std::max(relativeWidth * std::abs(threshold), kMinWidth);
    }
    [[nodiscard]] Test temperatureAtMost(float threshold) const {
        return atMost(temperature, threshold, temperatureWidth);
    }
    [[nodiscard]] Test humidityAtMost(float threshold) const {
        return atMost(humidity, threshold, humidityWidth);
    }
    [[nodiscard]] Test slopeAtMost(float threshold) const {
        return atMost(slope, threshold, widthOf(threshold));
    }
    [[nodiscard]] Test steepestAtLeast(float threshold) const {
        return atLeast(steepest, threshold, widthOf(threshold));
    }
    [[nodiscard]] Test altitudeAtMost(float threshold) const {
        return atMost(altitude, threshold, widthOf(threshold));
    }
};

// isSnow, isMountain, isBeach and isMarsh of biome.cpp.
Test snowTest(const Column& c, const BiomeParams& p) {
    return both(c.temperatureAtMost(p.snowTemperature), c.slopeAtMost(p.snowMaxSlope));
}

Test mountainTest(const Column& c, const BiomeParams& p) {
    const Test steep = both(negate(c.slopeAtMost(p.mountainSlope)), known(!c.wall));
    return both(negate(c.altitudeAtMost(p.mountainMinAltitude)),
                either(c.temperatureAtMost(p.treelineTemperature), steep));
}

// A beach is never below the sea level (dry land there is a depression) nor in a sea of dunes
// (inland, far from the coasts: ErgParams::coastNear), as in classifyBiome.
Test beachTest(const Column& c, const BiomeParams& p) {
    const Test low = both(negate(c.altitudeAtMost(0.0f)), c.altitudeAtMost(p.beachMaxHeight));
    return both(both(low, c.slopeAtMost(p.beachMaxSlope)), known(!(c.erg > 0.0f)));
}

Test marshTest(const Column& c, const BiomeParams& p) {
    const Test wet =
        either(atMost(c.riverDistance, p.marshRiverReach, c.widthOf(p.marshRiverReach)),
               c.altitudeAtMost(p.marshCoastHeight));
    const Test ground = both(c.slopeAtMost(p.marshMaxSlope), c.altitudeAtMost(p.marshMaxAltitude));
    const Test climate = both(negate(c.humidityAtMost(p.marshMinHumidity)),
                              negate(c.temperatureAtMost(p.marshMinTemperature)));
    return both(wet, both(ground, climate));
}

// The regions of whittakerBiome, in its order of decisions.
void whittakerMargins(const Column& c, const BiomeParams& p, std::array<float, kBiomeCount>& m) {
    const Test tundra = c.temperatureAtMost(p.tundraTemperature);
    const Test taiga = c.temperatureAtMost(p.taigaTemperature);
    const Test dryTaiga = c.humidityAtMost(p.taigaMinHumidity);
    const Test temperate = c.temperatureAtMost(p.tropicalTemperature);
    const auto set = [&m](Biome biome, float margin) {
        auto& slot = m[static_cast<std::size_t>(biome)];
        slot = std::min(slot, margin);
    };
    set(Biome::Tundra,
        std::min(tundra.holds, std::max({tundra.fails, taiga.holds, dryTaiga.holds})));
    set(Biome::Taiga, std::max({tundra.fails, taiga.holds, dryTaiga.fails}));
    const float mild = std::max({tundra.fails, taiga.fails, temperate.holds});
    const Test desert = c.humidityAtMost(p.temperateDesertHumidity);
    const Test grassland = c.humidityAtMost(p.forestHumidity);
    set(Biome::Desert, std::max(mild, desert.holds));
    set(Biome::Grassland, std::max({mild, desert.fails, grassland.holds}));
    set(Biome::TemperateForest, std::max({mild, desert.fails, grassland.fails}));
    const float hot = std::max({tundra.fails, taiga.fails, temperate.fails});
    const Test hotDesert = c.humidityAtMost(p.hotDesertHumidity);
    const Test savanna = c.humidityAtMost(p.rainforestHumidity);
    set(Biome::Desert, std::max(hot, hotDesert.holds));
    set(Biome::Savanna, std::max({hot, hotDesert.fails, savanna.holds}));
    set(Biome::TropicalForest, std::max({hot, hotDesert.fails, savanna.fails}));
}

// The regions of classifyBiome on land (no ocean): each decision in its order of priority,
// once every earlier one failed.
std::array<float, kBiomeCount> landBiomeMargins(const Column& c, const BiomeParams& p) {
    std::array<float, kBiomeCount> m{};
    m.fill(kRuledOut);
    float earlier = 0.0f; // margin of the failure of every earlier decision
    const auto decide = [&](Biome biome, Test test) {
        m[static_cast<std::size_t>(biome)] = std::max(earlier, test.holds);
        earlier = std::max(earlier, test.fails);
    };
    decide(Biome::Snow, snowTest(c, p));
    decide(Biome::Mountain, mountainTest(c, p));
    decide(Biome::Beach, beachTest(c, p));
    decide(Biome::Marsh, marshTest(c, p));
    std::array<float, kBiomeCount> whittaker{};
    whittaker.fill(kRuledOut);
    whittakerMargins(c, p, whittaker);
    for (const Biome biome : {Biome::Tundra, Biome::Taiga, Biome::Desert, Biome::Grassland,
                              Biome::TemperateForest, Biome::Savanna, Biome::TropicalForest}) {
        const auto b = static_cast<std::size_t>(biome);
        m[b] = std::max(earlier, whittaker[b]);
    }
    return m;
}

// The materials of the biomes whose ground is mostly mineral (mineralMaterial of
// surface_material.cpp) and of the marsh, but the snow out of the ground of the volcanoes (the
// margin of its failure: notVolcanic); false for the other biomes.
bool mineralMargins(const Column& c, const MaterialParams& p, Biome biome, float base,
                    float notVolcanic, std::array<float, kSurfaceMaterialCount>& m) {
    const auto offer = [&m](SurfaceMaterial material, float margin) {
        auto& slot = m[static_cast<std::size_t>(material)];
        slot = std::min(slot, margin);
    };
    switch (biome) {
    case Biome::Snow:
        offer(SurfaceMaterial::Snow, base);
        return true;
    case Biome::Mountain: {
        const Test snow = c.temperatureAtMost(p.mountainSnowTemperature);
        const Test scree = c.steepestAtLeast(p.screeSlope);
        offer(SurfaceMaterial::Snow, std::max(base, snow.holds));
        offer(SurfaceMaterial::Pebbles, std::max({base, snow.fails, scree.holds, notVolcanic}));
        offer(SurfaceMaterial::Rock, std::max({base, snow.fails, scree.fails, notVolcanic}));
        return true;
    }
    case Biome::Beach: {
        const Test cold = c.temperatureAtMost(p.pebbleBeachTemperature);
        offer(SurfaceMaterial::Pebbles, std::max({base, cold.holds, notVolcanic}));
        offer(SurfaceMaterial::Sand, std::max({base, cold.fails, notVolcanic}));
        return true;
    }
    case Biome::Desert:
        offer(SurfaceMaterial::Sand, std::max(base, notVolcanic));
        return true;
    case Biome::Marsh:
        offer(SurfaceMaterial::Mud, std::max(base, notVolcanic));
        return true;
    default:
        return false;
    }
}

// The floor of an erg between its dunes (ergFloorMaterial) over the margin before (what is not a
// dune nor rock), where the erg weighs enough away from the water; the test of that region.
Test ergFloorMargins(const Column& c, const MaterialParams& p, float before,
                     std::array<float, kSurfaceMaterialCount>& m) {
    const Test erg = both(atLeast(c.erg, p.ergFloorErg, c.widthOf(p.ergFloorErg)),
                          negate(atMost(c.riverDistance, p.ergGrassReach,
                                        c.widthOf(p.ergGrassReach))));
    const float base = std::max(before, erg.holds);
    const Test sand = negate(atLeast(c.ergFloor, 0.5f, p.ergFloorWidth));
    const Test crust = negate(atMost(c.ergFloor, 1.5f, p.ergFloorWidth));
    const auto offer = [&m](SurfaceMaterial material, float margin) {
        auto& slot = m[static_cast<std::size_t>(material)];
        slot = std::min(slot, margin);
    };
    offer(SurfaceMaterial::Sand, std::max(base, sand.holds));
    offer(SurfaceMaterial::Pebbles, std::max({base, sand.fails, crust.fails}));
    offer(SurfaceMaterial::Dirt, std::max({base, crust.holds}));
    return erg;
}

// The regions of surfaceMaterial on dry ground: the sand of the dunes first (but on walls), then
// rock, then the material of each biome over the region of that biome.
std::array<float, kSurfaceMaterialCount>
dryMaterialMargins(const Column& c, const MaterialParams& p,
                   const std::array<float, kBiomeCount>& biomes) {
    std::array<float, kSurfaceMaterialCount> m{};
    m.fill(kRuledOut);
    // The dunes are sand whatever their slope and their biome, but on walls and rock above air.
    const Test wall = known(c.wall || c.rockAboveAir);
    // Wide in the ergs only (the noise of the borders widens the bands up to 5 times: off them,
    // no sand where no dune is).
    const Test dune = atLeast(c.dunes, p.duneSand,
                              c.erg > 0.0f ? p.duneSandWidth : c.widthOf(p.duneSand));
    m[static_cast<std::size_t>(SurfaceMaterial::Sand)] = std::max(wall.fails, dune.holds);
    const float open = std::min(wall.holds, dune.fails); // the margin of what is not a dune
    const Test rock = either(c.steepestAtLeast(p.rockSlope), wall);
    m[static_cast<std::size_t>(SurfaceMaterial::Rock)] = std::max(open, rock.holds);
    const auto offer = [&m](SurfaceMaterial material, float margin) {
        auto& slot = m[static_cast<std::size_t>(material)];
        slot = std::min(slot, margin);
    };
    const float floors = std::max(open, rock.fails);
    const Test erg = ergFloorMargins(c, p, floors, m);
    const float beyond = std::max(floors, erg.fails);
    const Test bank = atMost(c.riverDistance, p.riverBankWidth, c.widthOf(p.riverBankWidth));
    // The ground of the volcanoes, out of the snow.
    const Test volcanic = atLeast(c.volcanicShare, p.volcanicShare, p.volcanicWidth);
    for (int b = 0; b < kBiomeCount; ++b) {
        const auto biome = static_cast<Biome>(b);
        const float base = std::max(beyond, biomes[static_cast<std::size_t>(b)]);
        if (biome == Biome::Ocean || base == kRuledOut) {
            continue;
        }
        // Not a margin out of the volcanoes: there, every region keeps its bits.
        if (biome != Biome::Snow && c.volcanicShare > 0.0f) {
            const float snow = biome == Biome::Mountain
                                   ? c.temperatureAtMost(p.mountainSnowTemperature).fails
                                   : 0.0f;
            offer(volcanicMaterial(c.volcanicGround), std::max({base, snow, volcanic.holds}));
        }
        if (mineralMargins(c, p, biome, base, volcanic.fails, m)) {
            continue;
        }
        const float soil = std::max(base, volcanic.fails);
        const Test dirt = biome == Biome::Tundra ? known(true) : c.steepestAtLeast(p.dirtSlope);
        offer(SurfaceMaterial::Pebbles, std::max(soil, bank.holds));
        offer(SurfaceMaterial::Dirt, std::max({soil, bank.fails, dirt.holds}));
        offer(SurfaceMaterial::Grass, std::max({soil, bank.fails, dirt.fails}));
    }
    return m;
}

// The materials of a site of bare rock (SurfaceSite::bareRock): rock alone, no blend.
void bareRockOnly(const SurfaceSite& site, Margins& margins) {
    if (site.bareRock) {
        margins.materials.fill(kRuledOut);
        margins.materials[static_cast<std::size_t>(SurfaceMaterial::Rock)] = 0.0f;
    }
}

} // namespace

Margins regionMargins(const ClimateSample& climate, const SurfaceSite& site,
                      const BiomeParams& biomes, const MaterialParams& materials,
                      const BlendParams& params) {
    Margins margins;
    if (site.water == WaterKind::Ocean) {
        margins.biomes.fill(kRuledOut);
        margins.materials.fill(kRuledOut);
        margins.biomes[static_cast<std::size_t>(Biome::Ocean)] = 0.0f;
        margins.materials[static_cast<std::size_t>(SurfaceMaterial::Water)] = 0.0f;
        bareRockOnly(site, margins);
        return margins;
    }
    const Column column{climate.temperature,     climate.humidity,
                        climate.slope,           steepestSlope(climate, site),
                        site.ground - kSeaLevel, site.riverDistance,
                        site.cliffWall,          site.rockAboveAir,
                        params.temperatureWidth, params.humidityWidth,
                        params.relativeWidth,    site.dunes,
                        site.erg,                site.ergFloor,
                        site.volcanicShare,      site.volcanicGround};
    margins.biomes = landBiomeMargins(column, biomes);
    if (site.dry()) {
        margins.materials = dryMaterialMargins(column, materials, margins.biomes);
    } else {
        margins.materials.fill(kRuledOut);
        margins.materials[static_cast<std::size_t>(SurfaceMaterial::Water)] = 0.0f;
    }
    bareRockOnly(site, margins);
    return margins;
}

} // namespace terrain::blend
