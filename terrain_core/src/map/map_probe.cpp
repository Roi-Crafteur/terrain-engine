#include "terrain/map/map_probe.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/map/map_source.hpp"
#include "terrain/world_size.hpp"

#include <array>

namespace terrain {

MapProbe probeAt(const MapWorldSource& source, double x, double z) {
    const Heightfield& field = source.field();
    MapProbe p;
    p.x = wrapCoordinate(x, field.world().x);
    p.z = wrapCoordinate(z, field.world().z);
    const HeightSample h = field.sampleAt(p.x, p.z);
    p.ground = h.height;
    p.water = h.water;
    p.waterLevel = h.water == WaterKind::None ? kNoWaterLevel : h.waterLevel;
    // The site of the ground as the far horizon builds it (horizon_sampler.cpp: classify), the
    // slopes of its 4 neighbors kMapProbeSlopeStep meters away (east, west, south, north).
    const double s = kMapProbeSlopeStep;
    const std::array<float, 4> around = {field.heightAt(p.x + s, p.z), field.heightAt(p.x - s, p.z),
                                         field.heightAt(p.x, p.z + s),
                                         field.heightAt(p.x, p.z - s)};
    SurfaceSite site = heightfieldSite(h.height, h.continentalness, around, s);
    site.water = h.water;
    site.waterLevel = h.waterLevel;
    const Ecology& ecology = source.ecology();
    const ClimateSample climate = ecology.climate().sample(p.x, p.z, site);
    p.biome = classifyBiome(climate, site, ecology.params().biomes);
    p.temperature = climate.temperature;
    p.humidity = climate.humidity;
    if (const PlanetLevel* planet = source.planet()) {
        const PlanetSample cell = planet->sampleAt(p.x, p.z);
        p.planet = true;
        p.discharge = cell.discharge;
        p.lake = cell.lake;
        p.precipitation = cell.precipitation;
    }
    return p;
}

} // namespace terrain
