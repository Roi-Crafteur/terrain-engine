#include "terrain/generation/cave_materials.hpp"

#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace terrain {

namespace {

static_assert(static_cast<long long>(kWorldAlignmentMeters) %
                      static_cast<long long>(kCaveEnvironmentSpacing) ==
                  0,
              "the nodes of the cave environment fall exactly on the sides of "
              "every world");

// Spacing of the one-pixel surface of a node: its gradient spans one pixel each side.
constexpr double kNodeMetersPerPixel = 4.0;

bool dryBiome(Biome biome) {
    return biome == Biome::Desert || biome == Biome::Savanna || biome == Biome::Beach;
}

bool mossyBiome(Biome biome) {
    return biome == Biome::TemperateForest || biome == Biome::TropicalForest ||
           biome == Biome::Marsh || biome == Biome::Taiga;
}

// Node indices and position of (x, z) between them.
struct Lattice {
    std::int64_t i;
    std::int64_t k;
    double fx;
    double fz;
};

Lattice latticeOf(double x, double z) {
    const double u = x / kCaveEnvironmentSpacing;
    const double v = z / kCaveEnvironmentSpacing;
    const double i = std::floor(u);
    const double k = std::floor(v);
    return {static_cast<std::int64_t>(i), static_cast<std::int64_t>(k), u - i, v - k};
}

// Bilinear temperature and humidity of the 4 nodes, biome of the nearest one.
CaveEnvironment interpolate(const std::array<CaveEnvironment, 4>& n, const Lattice& l) {
    const auto mix = [&l](float a, float b, float c, float d) {
        const double top = a + l.fx * (b - a);
        const double bottom = c + l.fx * (d - c);
        return static_cast<float>(top + l.fz * (bottom - top));
    };
    CaveEnvironment result;
    result.temperature =
        mix(n[0].temperature, n[1].temperature, n[2].temperature, n[3].temperature);
    result.humidity = mix(n[0].humidity, n[1].humidity, n[2].humidity, n[3].humidity);
    const std::size_t nearest = (l.fx < 0.5 ? 0U : 1U) + (l.fz < 0.5 ? 0U : 2U);
    result.biome = n[nearest].biome;
    return result;
}

std::uint64_t nodeKey(std::int64_t i, std::int64_t k) {
    return (static_cast<std::uint64_t>(i) << 32) ^ (static_cast<std::uint64_t>(k) & 0xFFFFFFFFU);
}

} // namespace

const char* caveMaterialName(CaveMaterial material) {
    switch (material) {
    case CaveMaterial::Surface:
        return "surface";
    case CaveMaterial::Stone:
        return "stone";
    case CaveMaterial::Deepstone:
        return "deepstone";
    case CaveMaterial::Limestone:
        return "limestone";
    case CaveMaterial::Sandstone:
        return "sandstone";
    case CaveMaterial::Ice:
        return "ice";
    case CaveMaterial::Moss:
        return "moss";
    case CaveMaterial::Mud:
        return "mud";
    case CaveMaterial::Gravel:
        return "gravel";
    case CaveMaterial::Water:
        return "water";
    case CaveMaterial::Basalt:
        return "basalt";
    case CaveMaterial::Magma:
        return "magma";
    }
    return "";
}

CaveMaterial caveMaterial(const CaveWallSite& site, const CaveMaterialParams& params) {
    const CaveEnvironment& environment = site.environment;
    const bool wet = site.aboveWater <= params.wetHeight;
    if (site.depth < params.surfaceDepth) {
        return CaveMaterial::Surface;
    }
    if (site.aboveLava <= params.magmaHeight) {
        return CaveMaterial::Magma;
    }
    if ((wet && site.frozenWater) || site.icyRegion ||
        (environment.temperature <= params.iceTemperature && site.depth <= params.iceDepth)) {
        return CaveMaterial::Ice;
    }
    if (wet) {
        return CaveMaterial::Mud;
    }
    if (site.volcanicRegion) {
        return CaveMaterial::Basalt;
    }
    if (site.lushRegion && site.depth <= params.lushDepth) {
        return CaveMaterial::Moss;
    }
    if (dryBiome(environment.biome)) {
        return CaveMaterial::Sandstone;
    }
    if (site.depth <= params.mossDepth && environment.humidity >= params.mossHumidity &&
        mossyBiome(environment.biome)) {
        return CaveMaterial::Moss;
    }
    if (site.normalY >= params.floorNormal) {
        return CaveMaterial::Gravel;
    }
    if (site.depth >= params.deepDepth) {
        return CaveMaterial::Deepstone;
    }
    if (environment.humidity >= params.limestoneHumidity) {
        return CaveMaterial::Limestone;
    }
    return CaveMaterial::Stone;
}

CaveEnvironmentField::CaveEnvironmentField(const Heightfield& field, const ClimateField& climate,
                                           const BiomeParams& biomes)
    : field_(&field), climate_(&climate), biomes_(biomes),
      nodesX_(periodicCells(field.world().x, kCaveEnvironmentSpacing)),
      nodesZ_(periodicCells(field.world().z, kCaveEnvironmentSpacing)) {
    if (!(climate.world() == field.world())) {
        throw std::invalid_argument("CaveEnvironmentField: the climate and the heightfield "
                                    "must share their world");
    }
}

CaveEnvironment CaveEnvironmentField::node(std::int64_t i, std::int64_t k) const {
    const double x = static_cast<double>(nodesX_.wrap(i)) * nodesX_.size;
    const double z = static_cast<double>(nodesZ_.wrap(k)) * nodesZ_.size;
    // A one-pixel surface centered on the node: its sample lies exactly at (x, z).
    const TerrainSurface surface = surfaceFromHeightfield(*field_, {x, z, kNodeMetersPerPixel, 1});
    const SurfaceSite& site = surface.sites.front();
    const ClimateSample climate = climate_->sample(x, z, site);
    return {classifyBiome(climate, site, biomes_), climate.temperature, climate.humidity};
}

CaveEnvironment CaveEnvironmentField::at(double x, double z) const {
    const Lattice l = latticeOf(x, z);
    return interpolate({node(l.i, l.k), node(l.i + 1, l.k), node(l.i, l.k + 1),
                        node(l.i + 1, l.k + 1)},
                       l);
}

const CaveEnvironment& CaveEnvironmentCache::node(std::int64_t i, std::int64_t k) {
    const auto [it, inserted] = nodes_.try_emplace(nodeKey(i, k));
    if (inserted) {
        it->second = field_->node(i, k);
    }
    return it->second;
}

CaveEnvironment CaveEnvironmentCache::at(double x, double z) {
    const Lattice l = latticeOf(x, z);
    return interpolate({node(l.i, l.k), node(l.i + 1, l.k), node(l.i, l.k + 1),
                        node(l.i + 1, l.k + 1)},
                       l);
}

} // namespace terrain
