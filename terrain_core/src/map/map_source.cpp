#include "terrain/map/map_source.hpp"

#include "map_water.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace terrain {

namespace {

std::size_t gridColumns(const MapFrame& f) {
    return static_cast<std::size_t>(f.width() + 2);
}

// The continentalness the climate reads in the cells of the planet level, where the relief is
// not sampled: only the climate without the humidity of the planet reads it.
constexpr float kPlanetLandContinentalness = 0.5f;
constexpr float kPlanetSeaContinentalness = -1.0f;

void validateParams(const MapSourceParams& p) {
    if (!(p.fineMostMetersPerPixel > 0.0) ||
        !(p.planetLeastMetersPerPixel > p.fineMostMetersPerPixel) ||
        !(p.planetRiverDischargePerMeter >= 0.0) || !(p.fallRingPixels >= p.fallCenterPixels) ||
        !(p.fallCenterPixels >= 0.0) || !(p.riverLeastPixels >= 0.0) ||
        !(p.fallStretchMeters >= 0.0) || !(p.planetFallDropPerMeter >= 0.0)) {
        throw std::invalid_argument("map source: invalid parameters");
    }
}

} // namespace

MapWorldSource::MapWorldSource(const TerrainTiles& tiles, const MapSourceParams& params)
    : MapWorldSource(tiles.heightfield(), tiles.ecology(), params) {}

MapWorldSource::MapWorldSource(const Heightfield& field, const Ecology& ecology,
                               const MapSourceParams& params)
    : params_(params), field_(&field), relief_(&field), ecology_(&ecology),
      hydrology_(field.hydrology()) {
    validateParams(params);
    if (!(ecology.world() == field.world())) {
        throw std::invalid_argument("map source: the field and the ecology tile different worlds");
    }
    if (hydrology_ != nullptr) {
        relief_ = &hydrology_->relief();
        planet_ = &hydrology_->planet();
    }
}

MapSourceKind MapWorldSource::kindFor(double metersPerPixel) const {
    if (metersPerPixel <= params_.fineMostMetersPerPixel) {
        return MapSourceKind::FineRelief;
    }
    if (planet_ != nullptr && metersPerPixel >= params_.planetLeastMetersPerPixel) {
        return MapSourceKind::PlanetCells;
    }
    return MapSourceKind::Relief;
}

void MapWorldSource::sample(const MapFrame& frame, std::span<MapSample> out, int threads,
                            MapSampleStats& stats) const {
    if (out.size() != mapGridSize(frame)) {
        throw std::invalid_argument("map source: the samples of another frame");
    }
    if (!(frame.world() == field_->world())) {
        throw std::invalid_argument("map source: a frame of another world");
    }
    const double step = std::max(frame.stepX(), frame.stepZ());
    const MapSourceKind kind = kindFor(step);
    if (kind == MapSourceKind::PlanetCells) {
        samplePlanet(frame, out, threads, stats);
    } else {
        sampleRelief(frame, kind == MapSourceKind::FineRelief ? *field_ : *relief_, out, threads,
                     stats);
        if (kind == MapSourceKind::Relief && planet_ != nullptr) {
            addPlanetWater(frame, out, threads);
        }
    }
    std::vector<map_water::Course> courses;
    std::vector<map_water::Fall> falls;
    if (kind == MapSourceKind::FineRelief) {
        if (hydrology_ != nullptr) {
            map_water::reachCourses(*hydrology_, frame, params_.fallStretchMeters, courses, falls,
                                    stats);
        }
    } else if (planet_ != nullptr) {
        const double least = hydrology_ != nullptr ? hydrology_->params().minDischarge : 0.0;
        map_water::planetCourses(*planet_, frame,
                                 std::max(least, params_.planetRiverDischargePerMeter * step),
                                 std::max(params_.fallLeastDrop,
                                          params_.planetFallDropPerMeter * step),
                                 courses, falls);
    }
    map_water::drawCourses(frame, courses, params_.riverLeastPixels, out);
    map_water::drawFalls(frame, falls, params_.fallCenterPixels, params_.fallRingPixels,
                         params_.fallLeastDrop, out);
    for (MapSample& s : out) {
        s.source = kind;
    }
    stats.samples += out.size();
}

// The relief of field at every sample, row by row through its batch reading (the same bits as
// sampleAt point by point), then the biomes.
void MapWorldSource::sampleRelief(const MapFrame& frame, const Heightfield& field,
                                  std::span<MapSample> out, int threads,
                                  MapSampleStats& stats) const {
    const std::size_t columns = gridColumns(frame);
    std::vector<float> continentalness(out.size());
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        std::vector<double> xs(columns);
        std::vector<double> zs(columns);
        std::vector<HeightSample> samples(columns);
        for (std::size_t i = 0; i < columns; ++i) {
            xs[i] = frame.pixelX(static_cast<std::int64_t>(i) - 1);
        }
        for (int row = begin; row < end; ++row) {
            std::fill(zs.begin(), zs.end(), frame.pixelZ(row - 1));
            field.samplesAt(xs, zs, samples);
            for (std::size_t i = 0; i < columns; ++i) {
                const std::size_t at = static_cast<std::size_t>(row) * columns + i;
                const HeightSample& h = samples[i];
                MapSample& s = out[at];
                s = MapSample{};
                s.ground = h.height;
                s.water = h.water;
                s.waterLevel = h.water == WaterKind::None ? kNoWaterLevel : h.waterLevel;
                continentalness[at] = h.continentalness;
            }
        }
    });
    stats.reliefSamples += out.size();
    if (field.hydrology() != nullptr) {
        stats.hydrologyCalls += out.size();
    }
    addBiomes(frame, continentalness, out, threads);
}

// The cells of the planet level: the bilinear altitude, the sea and the lakes of the cell
// holding each sample; then the biomes.
void MapWorldSource::samplePlanet(const MapFrame& frame, std::span<MapSample> out, int threads,
                                  MapSampleStats& stats) const {
    const std::size_t columns = gridColumns(frame);
    std::vector<float> continentalness(out.size());
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        for (int row = begin; row < end; ++row) {
            const double z = frame.pixelZ(row - 1);
            for (std::size_t i = 0; i < columns; ++i) {
                const std::size_t at = static_cast<std::size_t>(row) * columns + i;
                const PlanetSample p =
                    planet_->sampleAt(frame.pixelX(static_cast<std::int64_t>(i) - 1), z);
                MapSample& s = out[at];
                s = MapSample{};
                s.ground = p.elevation;
                if (p.ocean) {
                    s.water = WaterKind::Ocean;
                    s.waterLevel = kSeaLevel;
                } else if (p.lake >= 0) {
                    const PlanetLake& lake = planet_->lakes()[static_cast<std::size_t>(p.lake)];
                    s.water = lake.playa ? WaterKind::SaltLake : WaterKind::Lake;
                    s.waterLevel = p.waterSurface;
                }
                continentalness[at] =
                    p.ocean ? kPlanetSeaContinentalness : kPlanetLandContinentalness;
            }
        }
    });
    stats.planetSamples += out.size();
    addBiomes(frame, continentalness, out, threads);
}

// The biome of every pixel (not of the border) as the far horizon classifies its samples: the
// site of its ground with the slopes of its 4 neighbor pixels and its water, its climate, then
// classifyBiome (streaming/horizon_sampler.cpp, at the spacing of the pixels).
void MapWorldSource::addBiomes(const MapFrame& frame, std::span<const float> continentalness,
                               std::span<MapSample> out, int threads) const {
    const BiomeParams& biomes = ecology_->params().biomes;
    planet::parallelRows(frame.height(), threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            const double z = frame.pixelZ(j);
            for (int i = 0; i < frame.width(); ++i) {
                const std::size_t at = mapGridIndex(frame, i, j);
                MapSample& s = out[at];
                const std::array<float, 4> around = {
                    out[mapGridIndex(frame, i + 1, j)].ground,
                    out[mapGridIndex(frame, i - 1, j)].ground,
                    out[mapGridIndex(frame, i, j + 1)].ground,
                    out[mapGridIndex(frame, i, j - 1)].ground};
                SurfaceSite site =
                    heightfieldSite(s.ground, continentalness[at], around, frame.stepX());
                site.water = s.water;
                site.waterLevel = s.water == WaterKind::None ? s.ground : s.waterLevel;
                const ClimateSample climate = ecology_->climate().sample(frame.pixelX(i), z, site);
                s.biome = classifyBiome(climate, site, biomes);
            }
        }
    });
}

// The lakes of the planet level over the relief alone: where the cell of a sample is under a
// lake and the relief lies below its level.
void MapWorldSource::addPlanetWater(const MapFrame& frame, std::span<MapSample> out,
                                    int threads) const {
    const std::size_t columns = gridColumns(frame);
    const PlanetGrid& grid = planet_->grid();
    const PlanetCells& cells = planet_->cells();
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        for (int row = begin; row < end; ++row) {
            const double z = frame.pixelZ(row - 1);
            for (std::size_t i = 0; i < columns; ++i) {
                MapSample& s = out[static_cast<std::size_t>(row) * columns + i];
                if (s.water != WaterKind::None) {
                    continue;
                }
                const std::int32_t lake = cells.lakeAt(
                    planetCellAt(grid, frame.pixelX(static_cast<std::int64_t>(i) - 1), z));
                if (lake < 0) {
                    continue;
                }
                const PlanetLake& l = planet_->lakes()[static_cast<std::size_t>(lake)];
                if (s.ground < l.level) {
                    s.water = l.playa ? WaterKind::SaltLake : WaterKind::Lake;
                    s.waterLevel = l.level;
                }
            }
        }
    });
}

} // namespace terrain
