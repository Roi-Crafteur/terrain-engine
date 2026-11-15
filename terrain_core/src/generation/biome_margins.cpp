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
    float baseTemperature; // at sea level (ClimateSample::baseTemperature)
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
    float coast;    // coastDistance: meters to the coast, positive inland

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
    [[nodiscard]] Test baseAtMost(float threshold) const {
        return atMost(baseTemperature, threshold, temperatureWidth);
    }
    [[nodiscard]] Test coastAtMost(float threshold) const {
        return atMost(coast, threshold, widthOf(threshold));
    }
};

// coldBiome, isMountain (with the high mountain), isBeach and isMarsh of biome.cpp.
Test polarColdTest(const Column& c, const BiomeParams& p) {
    return c.baseAtMost(p.snowTemperature + p.polarBaseOffset);
}

Test mountainTest(const Column& c, const BiomeParams& p) {
    const Test steep = both(negate(c.slopeAtMost(p.mountainSlope)), known(!c.wall));
    return both(negate(c.altitudeAtMost(p.mountainMinAltitude)),
                either(c.temperatureAtMost(p.treelineTemperature), steep));
}

// A beach is never below the sea level (dry land there is a depression), in a sea of dunes
// (inland, far from the coasts: ErgParams::coastNear) nor far from the sea, as in classifyBiome.
Test beachTest(const Column& c, const BiomeParams& p) {
    const Test low = both(negate(c.altitudeAtMost(0.0f)), c.altitudeAtMost(p.beachMaxHeight));
    const Test near = both(c.slopeAtMost(p.beachMaxSlope), c.coastAtMost(p.beachCoastReach));
    return both(both(low, near), known(!(c.erg > 0.0f)));
}

Test marshTest(const Column& c, const BiomeParams& p) {
    const Test coast = both(c.altitudeAtMost(p.marshCoastHeight), c.coastAtMost(p.marshCoastReach));
    const Test wet =
        either(atMost(c.riverDistance, p.marshRiverReach, c.widthOf(p.marshRiverReach)), coast);
    const Test ground = both(c.slopeAtMost(p.marshMaxSlope), c.altitudeAtMost(p.marshMaxAltitude));
    const Test climate = both(negate(c.humidityAtMost(p.marshMinHumidity)),
                              negate(c.temperatureAtMost(p.marshMinTemperature)));
    return both(wet, both(ground, climate));
}

using BiomeMargins = std::array<float, kBiomeCount>;

// The margin of a biome reached by one more path of the decisions: the least of its paths.
void offerBiome(BiomeMargins& m, Biome biome, float margin) {
    auto& slot = m[static_cast<std::size_t>(biome)];
    slot = std::min(slot, margin);
}

// The cold and the cool rows of whittakerBiome (below taigaTemperature).
void coldWhittaker(const Column& c, const BiomeParams& p, BiomeMargins& m) {
    const Test tundra = c.temperatureAtMost(p.tundraTemperature);
    const Test taiga = c.temperatureAtMost(p.taigaTemperature);
    const Test dry = c.humidityAtMost(p.coldDesertHumidity);
    const Test dryTaiga = c.humidityAtMost(p.taigaMinHumidity);
    offerBiome(m, Biome::ColdDesert, std::max(tundra.holds, dry.holds));
    offerBiome(m, Biome::Tundra, std::max(tundra.holds, dry.fails));
    const float cool = std::max(tundra.fails, taiga.holds);
    offerBiome(m, Biome::ColdDesert, std::max(cool, dry.holds));
    offerBiome(m, Biome::Tundra, std::max({cool, dry.fails, dryTaiga.holds}));
    offerBiome(m, Biome::Taiga, std::max(cool, dryTaiga.fails));
}

// The temperate row of whittakerBiome (temperateBiome).
void temperateWhittaker(const Column& c, const BiomeParams& p, BiomeMargins& m) {
    const float mild = std::max({c.temperatureAtMost(p.tundraTemperature).fails,
                                 c.temperatureAtMost(p.taigaTemperature).fails,
                                 c.temperatureAtMost(p.tropicalTemperature).holds});
    const Test desert = c.humidityAtMost(p.temperateDesertHumidity);
    const Test coldDesert = c.temperatureAtMost(p.coldDesertTemperature);
    const Test steppe = c.humidityAtMost(p.steppeHumidity);
    const Test grassland = c.humidityAtMost(p.forestHumidity);
    const Test cool = c.temperatureAtMost(p.mediterraneanTemperature);
    offerBiome(m, Biome::ColdDesert, std::max({mild, desert.holds, coldDesert.holds}));
    offerBiome(m, Biome::RockyDesert, std::max({mild, desert.holds, coldDesert.fails}));
    offerBiome(m, Biome::Steppe, std::max({mild, desert.fails, steppe.holds}));
    offerBiome(m, Biome::Grassland, std::max({mild, steppe.fails, grassland.holds, cool.holds}));
    offerBiome(m, Biome::Mediterranean,
               std::max({mild, steppe.fails, grassland.holds, cool.fails}));
    offerBiome(m, Biome::TemperateForest, std::max(mild, grassland.fails));
}

// The hot row of whittakerBiome (hotBiome).
void hotWhittaker(const Column& c, const BiomeParams& p, BiomeMargins& m) {
    const float hot = std::max({c.temperatureAtMost(p.tundraTemperature).fails,
                                c.temperatureAtMost(p.taigaTemperature).fails,
                                c.temperatureAtMost(p.tropicalTemperature).fails});
    const Test desert = c.humidityAtMost(p.hotDesertHumidity);
    const Test shrubland = c.humidityAtMost(p.shrublandHumidity);
    const Test savanna = c.humidityAtMost(p.dryForestHumidity);
    const Test dryForest = c.humidityAtMost(p.rainforestHumidity);
    offerBiome(m, Biome::RockyDesert, std::max(hot, desert.holds));
    offerBiome(m, Biome::Shrubland, std::max({hot, desert.fails, shrubland.holds}));
    offerBiome(m, Biome::Savanna, std::max({hot, shrubland.fails, savanna.holds}));
    offerBiome(m, Biome::DryForest, std::max({hot, savanna.fails, dryForest.holds}));
    offerBiome(m, Biome::TropicalForest, std::max(hot, dryForest.fails));
}

// The biomes of the cold (coldBiome): the polar region where it is too dry for ice, then snow on
// gentle ground, the ice desert where the cold comes from the latitude. Returns the margin of the
// failure of all of them.
float coldMargins(const Column& c, const BiomeParams& p, BiomeMargins& m) {
    const Test below = c.temperatureAtMost(p.snowTemperature);
    const Test polarDry = both(polarColdTest(c, p), c.humidityAtMost(p.polarDryHumidity));
    const Test polar = both(below, polarDry);
    offerBiome(m, Biome::Polar, polar.holds);
    const Test snow = both(below, c.slopeAtMost(p.snowMaxSlope));
    const Test sheet = c.baseAtMost(p.snowTemperature + p.iceDesertBaseOffset);
    const float snowRegion = std::max(polar.fails, snow.holds);
    offerBiome(m, Biome::IceDesert, std::max(snowRegion, sheet.holds));
    offerBiome(m, Biome::Snow, std::max(snowRegion, sheet.fails));
    return std::max(polar.fails, snow.fails);
}

// The regions of classifyBiome on land (no ocean): each decision in its order of priority,
// once every earlier one failed.
BiomeMargins landBiomeMargins(const Column& c, const BiomeParams& p) {
    BiomeMargins m{};
    m.fill(kRuledOut);
    float earlier = coldMargins(c, p, m); // margin of the failure of every earlier decision
    const auto decide = [&](Biome biome, Test test) {
        offerBiome(m, biome, std::max(earlier, test.holds));
        earlier = std::max(earlier, test.fails);
    };
    // The mountain, its coldest part the high mountain: one region split in two, as the snow and
    // the ice desert (no corner where four biomes meet).
    const Test mountain = mountainTest(c, p);
    const Test high = c.temperatureAtMost(p.snowTemperature + p.highMountainOffset);
    offerBiome(m, Biome::HighMountain, std::max({earlier, mountain.holds, high.holds}));
    offerBiome(m, Biome::Mountain, std::max({earlier, mountain.holds, high.fails}));
    earlier = std::max(earlier, mountain.fails);
    decide(Biome::Polar, both(polarColdTest(c, p), c.temperatureAtMost(p.tundraTemperature)));
    decide(Biome::Beach, beachTest(c, p));
    decide(Biome::Marsh, marshTest(c, p));
    decide(Biome::SandDesert, atLeast(c.erg, p.sandDesertErg, c.widthOf(p.sandDesertErg)));
    BiomeMargins whittaker{};
    whittaker.fill(kRuledOut);
    coldWhittaker(c, p, whittaker);
    temperateWhittaker(c, p, whittaker);
    hotWhittaker(c, p, whittaker);
    for (std::size_t b = 0; b < m.size(); ++b) {
        if (whittaker[b] != kRuledOut) {
            m[b] = std::min(m[b], std::max(earlier, whittaker[b]));
        }
    }
    return m;
}

using MaterialMargins = std::array<float, kSurfaceMaterialCount>;

void offerMaterial(MaterialMargins& m, SurfaceMaterial material, float margin) {
    auto& slot = m[static_cast<std::size_t>(material)];
    slot = std::min(slot, margin);
}

// desertMaterial of surface_material.cpp over the region base of a desert.
void desertMargins(const Column& c, const MaterialParams& p, Biome biome, float base,
                   MaterialMargins& m) {
    const Test near = atMost(c.riverDistance, p.desertGrassReach, c.widthOf(p.desertGrassReach));
    const Test bank = atMost(c.riverDistance, p.riverBankWidth, c.widthOf(p.riverBankWidth));
    const Test dirt = c.steepestAtLeast(p.dirtSlope);
    offerMaterial(m, SurfaceMaterial::Pebbles, std::max({base, near.holds, bank.holds}));
    offerMaterial(m, SurfaceMaterial::Dirt, std::max({base, near.holds, bank.fails, dirt.holds}));
    offerMaterial(m, SurfaceMaterial::Grass, std::max({base, near.holds, bank.fails, dirt.fails}));
    const float away = std::max(base, near.fails);
    if (biome == Biome::SandDesert) {
        offerMaterial(m, SurfaceMaterial::Sand, away);
        return;
    }
    const Test hamada = negate(c.altitudeAtMost(p.hamadaAltitude));
    offerMaterial(m, SurfaceMaterial::Stone, std::max(away, hamada.holds));
    offerMaterial(m, SurfaceMaterial::Gravel, std::max(away, hamada.fails));
}

// The materials of the biomes whose ground is mostly mineral (mineralMaterial of
// surface_material.cpp) and of the marsh; false for the other biomes.
bool mineralMargins(const Column& c, const MaterialParams& p, Biome biome, float base,
                    MaterialMargins& m) {
    switch (biome) {
    case Biome::Snow:
    case Biome::IceDesert:
        offerMaterial(m, SurfaceMaterial::Snow, base);
        return true;
    case Biome::Mountain:
    case Biome::HighMountain: {
        const Test snow = c.temperatureAtMost(p.mountainSnowTemperature);
        const Test scree = c.steepestAtLeast(p.screeSlope);
        offerMaterial(m, SurfaceMaterial::Snow, std::max(base, snow.holds));
        offerMaterial(m, SurfaceMaterial::Pebbles, std::max({base, snow.fails, scree.holds}));
        offerMaterial(m, SurfaceMaterial::Rock, std::max({base, snow.fails, scree.fails}));
        return true;
    }
    case Biome::Beach: {
        const Test cold = c.temperatureAtMost(p.pebbleBeachTemperature);
        offerMaterial(m, SurfaceMaterial::Pebbles, std::max(base, cold.holds));
        offerMaterial(m, SurfaceMaterial::Sand, std::max(base, cold.fails));
        return true;
    }
    case Biome::Polar:
        offerMaterial(m, SurfaceMaterial::Gravel, base);
        return true;
    case Biome::RockyDesert:
    case Biome::SandDesert:
    case Biome::ColdDesert:
        desertMargins(c, p, biome, base, m);
        return true;
    case Biome::Marsh:
        offerMaterial(m, SurfaceMaterial::Mud, base);
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
    offer(SurfaceMaterial::Gravel, std::max({base, sand.fails, crust.fails}));
    offer(SurfaceMaterial::Crust, std::max({base, crust.holds}));
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
    for (int b = 0; b < kBiomeCount; ++b) {
        const auto biome = static_cast<Biome>(b);
        const float base = std::max(beyond, biomes[static_cast<std::size_t>(b)]);
        if (biome == Biome::Ocean || base == kRuledOut || mineralMargins(c, p, biome, base, m)) {
            continue;
        }
        const Test dirt = biome == Biome::Tundra ? known(true) : c.steepestAtLeast(p.dirtSlope);
        offer(SurfaceMaterial::Pebbles, std::max(base, bank.holds));
        offer(SurfaceMaterial::Dirt, std::max({base, bank.fails, dirt.holds}));
        offer(SurfaceMaterial::Grass, std::max({base, bank.fails, dirt.fails}));
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
    const Column column{climate.temperature,     climate.baseTemperature,
                        climate.humidity,        climate.slope,
                        steepestSlope(climate, site), site.ground - kSeaLevel,
                        site.riverDistance,      site.cliffWall,
                        site.rockAboveAir,       params.temperatureWidth,
                        params.humidityWidth,    params.relativeWidth,
                        site.dunes,              site.erg,
                        site.ergFloor,           coastDistance(site)};
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
