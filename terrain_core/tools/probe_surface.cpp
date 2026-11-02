// The section surface of terrain_probe (probe_sections.hpp): the biome, the material, the plants,
// the local climate and the weights of the blend of the column, from the site of the point and of
// its 4 neighbors one LOD 0 cell away, as the tiles build theirs (tile_blend.cpp).
#include "probe_sections.hpp"

#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/streaming/lod.hpp"

#include <array>
#include <cctype>

namespace probe {

namespace {

// "tall grass" -> "tallGrass": the name of a plant as a key.
std::string keyOf(const char* name) {
    std::string key;
    bool upper = false;
    for (const char* c = name; *c != '\0'; ++c) {
        if (*c == ' ') {
            upper = true;
            continue;
        }
        key += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(*c))) : *c;
        upper = false;
    }
    return key;
}

terrain::SurfaceSite siteOf(const ProbeWorld& w, const Point& p) {
    const double step = terrain::tileCellMeters(0);
    const auto h = [&](double dx, double dz) {
        return w.field().heightAt(wrapX(w, p.x + dx), wrapZ(w, p.z + dz));
    };
    // East (+x), west, south (+z), north.
    return terrain::heightfieldSite(p.ground, p.sample.continentalness,
                                    {h(step, 0.0), h(-step, 0.0), h(0.0, step), h(0.0, -step)},
                                    step);
}

template <class Id, class Name>
std::vector<Record> slots(const std::array<Id, terrain::kBlendSlots>& ids,
                          const std::array<float, terrain::kBlendSlots>& weights, Name name) {
    std::vector<Record> out;
    for (int i = 0; i < terrain::kBlendSlots; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (i == 0 || weights[k] > 0.0f) {
            Record r;
            r.text("name", name(ids[k])).number("weight", weights[k], 4);
            out.push_back(std::move(r));
        }
    }
    return out;
}

} // namespace

Record surfaceSection(const ProbeWorld& w, const Point& p) {
    const terrain::Ecology& ecology = w.tiles().ecology();
    const terrain::SurfaceSite site = siteOf(w, p);
    const terrain::ClimateSample climate = ecology.climate().sample(p.x, p.z, site);
    const terrain::EcologySample e = ecology.sampleAt(p.x, p.z, site);
    const terrain::SurfaceBlend blend = ecology.blendAt(p.x, p.z, site, climate);
    Record plants;
    for (int i = 0; i < terrain::kPlantTypeCount; ++i) {
        const auto type = static_cast<terrain::PlantType>(i);
        plants.number(keyOf(terrain::plantName(type)), e.densities[static_cast<std::size_t>(i)],
                      4);
    }
    Record r;
    r.text("biome", terrain::biomeName(e.biome))
        .text("material", terrain::materialName(e.material))
        .number("temperature", climate.temperature, 2, "C")
        .number("baseTemperature", climate.baseTemperature, 2, "C")
        .number("humidity", climate.humidity, 4)
        .number("slope", slopeDegrees(climate.slope), 2, "deg")
        .number("exposure", climate.exposure, 4)
        .add("plants", std::move(plants))
        .list("biomes", slots(blend.biomes, blend.biomeWeights, terrain::biomeName))
        .list("materials", slots(blend.materials, blend.materialWeights, terrain::materialName));
    return r;
}

} // namespace probe
