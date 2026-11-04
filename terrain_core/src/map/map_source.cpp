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
#include <array>
#include <cmath>
#include <iterator>
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

// The point a of n along each axis of the pixel i of a frame, spread evenly over it (its center
// for n = 1).
double subX(const MapFrame& frame, std::int64_t i, int a, int n) {
    return frame.pixelX(i) + ((a + 0.5) / n - 0.5) * frame.stepX();
}

double subZ(const MapFrame& frame, std::int64_t j, int b, int n) {
    return frame.pixelZ(j) + ((b + 0.5) / n - 0.5) * frame.stepZ();
}

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

WaterKind mapWaterOf(WaterKind water, float ground, float waterLevel) {
    const bool standing =
        water == WaterKind::Ocean || water == WaterKind::Lake || water == WaterKind::SaltLake;
    return standing && !(ground < waterLevel) ? WaterKind::None : water;
}

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
                            MapSampleStats& stats, MapCancel cancel) const {
    if (out.size() != mapGridSize(frame)) {
        throw std::invalid_argument("map source: the samples of another frame");
    }
    if (!(frame.world() == field_->world())) {
        throw std::invalid_argument("map source: a frame of another world");
    }
    const double step = std::max(frame.stepX(), frame.stepZ());
    const MapSourceKind kind = kindFor(step);
    if (kind == MapSourceKind::PlanetCells) {
        samplePlanet(frame, out, threads, stats, cancel);
    } else {
        sampleRelief(frame, kind == MapSourceKind::FineRelief ? *field_ : *relief_, out, threads,
                     stats, cancel);
        if (kind == MapSourceKind::Relief && planet_ != nullptr) {
            addPlanetWater(frame, out, threads, cancel);
        }
    }
    if (kind != MapSourceKind::FineRelief) {
        addSeaFloors(frame, out, threads, cancel);
    }
    if (mapCancelled(cancel)) {
        return;
    }
    std::vector<map_water::Course> courses;
    std::vector<map_water::Fall> falls;
    if (kind == MapSourceKind::FineRelief) {
        if (hydrology_ != nullptr) {
            map_water::reachCourses(*hydrology_, frame, params_.fallStretchMeters, courses, falls,
                                    stats, cancel);
        }
    } else if (planet_ != nullptr) {
        const double least = hydrology_ != nullptr ? hydrology_->params().minDischarge : 0.0;
        map_water::planetCourses(
            *planet_, frame, std::max(least, params_.planetRiverDischargePerMeter * step),
            std::max(params_.fallLeastDrop, params_.planetFallDropPerMeter * step), courses, falls);
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
                                  std::span<MapSample> out, int threads, MapSampleStats& stats,
                                  MapCancel cancel) const {
    const std::size_t columns = gridColumns(frame);
    std::vector<float> continentalness(out.size());
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        std::vector<double> xs(columns);
        std::vector<double> zs(columns);
        std::vector<HeightSample> samples(columns);
        for (std::size_t i = 0; i < columns; ++i) {
            xs[i] = frame.pixelX(static_cast<std::int64_t>(i) - 1);
        }
        for (int row = begin; row < end && !mapCancelled(cancel); ++row) {
            std::fill(zs.begin(), zs.end(), frame.pixelZ(row - 1));
            field.samplesAt(xs, zs, samples);
            for (std::size_t i = 0; i < columns; ++i) {
                const std::size_t at = static_cast<std::size_t>(row) * columns + i;
                const HeightSample& h = samples[i];
                MapSample& s = out[at];
                s = MapSample{};
                s.ground = h.height;
                s.water = mapWaterOf(h.water, h.height, h.waterLevel);
                s.waterLevel = s.water == WaterKind::None ? kNoWaterLevel : h.waterLevel;
                continentalness[at] = h.continentalness;
            }
        }
    });
    stats.reliefSamples += out.size();
    if (field.hydrology() != nullptr) {
        stats.hydrologyCalls += out.size();
    }
    addBiomes(frame, continentalness, out, threads, cancel, {}, 1);
}

int MapWorldSource::planetSubPoints(const MapFrame& frame) const {
    const double cells = std::max(frame.stepX(), frame.stepZ()) / planet_->grid().cellSize;
    return std::clamp(static_cast<int>(std::ceil(cells)), 1, kMapPlanetSubPoints);
}

namespace {

// The water most points of a pixel have (ties to the first in None, Ocean, Lake, SaltLake order),
// the mean of their levels, and the mean ground of all of them.
void aggregate(std::span<const MapSubPoint> points, MapSample& s) {
    std::array<int, 5> votes{};
    std::array<double, 5> levels{};
    double ground = 0.0;
    for (const MapSubPoint& p : points) {
        const auto k = static_cast<std::size_t>(p.water);
        ++votes[k];
        levels[k] += p.water == WaterKind::None ? 0.0 : static_cast<double>(p.waterLevel);
        ground += static_cast<double>(p.ground);
    }
    std::size_t best = 0;
    for (std::size_t k = 1; k < votes.size(); ++k) {
        best = votes[k] > votes[best] ? k : best;
    }
    s.ground = static_cast<float>(ground / static_cast<double>(points.size()));
    s.water = static_cast<WaterKind>(best);
    s.waterLevel = s.water == WaterKind::None
                       ? kNoWaterLevel
                       : static_cast<float>(levels[best] / static_cast<double>(votes[best]));
}

} // namespace

// The cells of the planet level: each pixel (and its border) from n x n points spread over it
// (planetSubPoints): their mean altitude (bilinear) and the water most of them have (the sea and
// the lakes of their cells); then the biome most of them have.
void MapWorldSource::samplePlanet(const MapFrame& frame, std::span<MapSample> out, int threads,
                                  MapSampleStats& stats, MapCancel cancel) const {
    const std::size_t columns = gridColumns(frame);
    const int n = planetSubPoints(frame);
    const auto per = static_cast<std::size_t>(n * n);
    std::vector<MapSubPoint> points(out.size() * per);
    std::vector<float> continentalness(out.size());
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        for (int row = begin; row < end && !mapCancelled(cancel); ++row) {
            for (std::size_t i = 0; i < columns; ++i) {
                const std::size_t at = static_cast<std::size_t>(row) * columns + i;
                const std::span<MapSubPoint> mine(points.data() + at * per, per);
                for (int b = 0; b < n; ++b) {
                    for (int a = 0; a < n; ++a) {
                        mine[static_cast<std::size_t>(b * n + a)] =
                            planetPoint(subX(frame, static_cast<std::int64_t>(i) - 1, a, n),
                                        subZ(frame, row - 1, b, n));
                    }
                }
                MapSample& s = out[at];
                s = MapSample{};
                aggregate(mine, s);
                continentalness[at] = s.water == WaterKind::Ocean ? kPlanetSeaContinentalness
                                                                  : kPlanetLandContinentalness;
            }
        }
    });
    stats.planetSamples += points.size();
    addBiomes(frame, continentalness, out, threads, cancel, {points.data(), points.size()}, n);
}

MapSubPoint MapWorldSource::planetPoint(double x, double z) const {
    const PlanetSample p = planet_->sampleAt(x, z);
    MapSubPoint point;
    point.ground = p.elevation;
    if (p.ocean) {
        point.water = WaterKind::Ocean;
        point.waterLevel = kSeaLevel;
    } else if (p.lake >= 0) {
        const PlanetLake& lake = planet_->lakes()[static_cast<std::size_t>(p.lake)];
        point.water = lake.playa ? WaterKind::SaltLake : WaterKind::Lake;
        point.waterLevel = p.waterSurface;
    }
    return point;
}

// The biome of every pixel (not of the border) as the far horizon classifies its samples: the
// site of its ground with the slopes of its 4 neighbor pixels and its water, its climate, then
// classifyBiome (streaming/horizon_sampler.cpp, at the spacing of the pixels). With points
// (n x n per pixel, the cells of the planet level), the biome most of them have, each classified
// with its own ground and water and the slopes of the pixel (ties to the first biome).
void MapWorldSource::addBiomes(const MapFrame& frame, std::span<const float> continentalness,
                               std::span<MapSample> out, int threads, MapCancel cancel,
                               std::span<const MapSubPoint> points, int n) const {
    const BiomeParams& biomes = ecology_->params().biomes;
    const auto per = static_cast<std::size_t>(n * n);
    planet::parallelRows(frame.height(), threads, [&](int begin, int end) {
        for (int j = begin; j < end && !mapCancelled(cancel); ++j) {
            for (int i = 0; i < frame.width(); ++i) {
                const std::size_t at = mapGridIndex(frame, i, j);
                MapSample& s = out[at];
                const std::array<float, 4> around = {out[mapGridIndex(frame, i + 1, j)].ground,
                                                     out[mapGridIndex(frame, i - 1, j)].ground,
                                                     out[mapGridIndex(frame, i, j + 1)].ground,
                                                     out[mapGridIndex(frame, i, j - 1)].ground};
                const auto classify = [&](double x, double z, float ground, WaterKind water,
                                          float level) {
                    SurfaceSite site =
                        heightfieldSite(ground, continentalness[at], around, frame.stepX());
                    site.water = water;
                    site.waterLevel = water == WaterKind::None ? ground : level;
                    return classifyBiome(ecology_->climate().sample(x, z, site), site, biomes);
                };
                if (points.empty()) {
                    s.biome =
                        classify(frame.pixelX(i), frame.pixelZ(j), s.ground, s.water, s.waterLevel);
                    continue;
                }
                std::array<int, kBiomeCount> votes{};
                for (int b = 0; b < n; ++b) {
                    for (int a = 0; a < n; ++a) {
                        const MapSubPoint& p =
                            points[at * per + static_cast<std::size_t>(b * n + a)];
                        ++votes[static_cast<std::size_t>(classify(subX(frame, i, a, n),
                                                                  subZ(frame, j, b, n), p.ground,
                                                                  p.water, p.waterLevel))];
                    }
                }
                s.biome = static_cast<Biome>(
                    std::distance(votes.begin(), std::max_element(votes.begin(), votes.end())));
            }
        }
    });
}

// The lakes of the planet level over the relief alone: where the cell of a sample is under a
// lake and the relief lies below its level.
void MapWorldSource::addPlanetWater(const MapFrame& frame, std::span<MapSample> out, int threads,
                                    MapCancel cancel) const {
    const std::size_t columns = gridColumns(frame);
    const PlanetGrid& grid = planet_->grid();
    const PlanetCells& cells = planet_->cells();
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        for (int row = begin; row < end && !mapCancelled(cancel); ++row) {
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

void MapWorldSource::addSeaFloors(const MapFrame& frame, std::span<MapSample> out, int threads,
                                  MapCancel cancel) const {
    const Seas* seas = hydrology_ != nullptr ? hydrology_->seas() : nullptr;
    if (seas == nullptr) {
        return;
    }
    const std::size_t columns = gridColumns(frame);
    const double footprint = std::max(frame.stepX(), frame.stepZ());
    const WorldSize& world = field_->world();
    planet::parallelRows(frame.height() + 2, threads, [&](int begin, int end) {
        for (int row = begin; row < end && !mapCancelled(cancel); ++row) {
            const double z = wrapCoordinate(frame.pixelZ(row - 1), world.z);
            for (std::size_t i = 0; i < columns; ++i) {
                MapSample& s = out[static_cast<std::size_t>(row) * columns + i];
                if (s.water != WaterKind::Ocean) {
                    continue;
                }
                const double x =
                    wrapCoordinate(frame.pixelX(static_cast<std::int64_t>(i) - 1), world.x);
                const SeaPoint p = seas->mapAt(x, z, footprint);
                if (p.zone) {
                    s.ground = static_cast<float>(p.floor);
                }
            }
        }
    });
}

} // namespace terrain
