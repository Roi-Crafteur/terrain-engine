#pragma once

#include "terrain/generation/water_kind.hpp"
#include "terrain/map/map_render.hpp"

#include <cstddef>
#include <span>

namespace terrain {

class Ecology;
class Heightfield;
class Hydrology;
class PlanetLevel;
class TerrainTiles;

// Where the world source reads a map, by the spacing of its pixels (MapSourceParams).
struct MapSourceParams {
    // Up to this many meters per pixel: the fine relief with its water (the valleys carved, the
    // rivers and lakes at their level: Heightfield::samplesAt with the hydrology), the courses of
    // the reaches near the box (Hydrology::reachesNear, reach) and their falls. Beyond it the
    // Hydrology is never queried (its courses cost too much over large boxes): the relief alone
    // and the water of the planet level.
    double fineMostMetersPerPixel = 64.0;
    // From this many meters per pixel on, with a planet level: its cells (bilinear altitude, sea,
    // lakes, climate) instead of the relief.
    double planetLeastMetersPerPixel = 2048.0;
    // The rivers of the planet level drawn at a spacing: those of at least this many m^3/s per
    // meter of a pixel (32 m^3/s at 128 m per pixel, 8,192 at 32 km), and never below the
    // smallest river of the hydrology.
    double planetRiverDischargePerMeter = 0.25;
    // The marks of the falls: a center and a ring of these radii in pixels, for the falls of at
    // least fallLeastDrop meters. Up to fineMostMetersPerPixel, a fall is a stretch of the steps
    // of a reach whose lips lie within fallStretchMeters of the next (the 388 m fall is one mark
    // of 388 m at its first lip). Beyond, the waterfalls of the planet level (steps of its cells)
    // of at least planetFallDropPerMeter meters per meter of a pixel (410 m at 8 km per pixel).
    double fallCenterPixels = 2.5;
    double fallRingPixels = 5.0;
    double fallLeastDrop = 5.0;
    double fallStretchMeters = 250.0;
    double planetFallDropPerMeter = 0.05;
    // The narrowest course drawn, in pixels.
    double riverLeastPixels = 1.0;
};

// The points per pixel along each axis, at most, in the cells of the planet level (n x n points
// spread over a pixel larger than a cell).
inline constexpr int kMapPlanetSubPoints = 3;

// The water a map shows over a sample of the core: none where the ground stands at or above the
// surface of a sea, a lake or a salt lake (the ground Unreal shows emerged, such as the banks of a
// valley the hydrology carries out into the sea); every other water as it is.
[[nodiscard]] WaterKind mapWaterOf(WaterKind water, float ground, float waterLevel);

// A point of a pixel of the cells of the planet level: its ground (bilinear) and the water of its
// cell.
struct MapSubPoint {
    float ground = 0.0f;
    WaterKind water = WaterKind::None;
    float waterLevel = kNoWaterLevel;
};

// The world as the map sees it: the relief, its water and its biomes (MapSampler). It reads what
// the streaming reads (the heightfield with its hydrology, the ecology, the planet level), never
// changes them, and may run on any thread at the same time as the streaming: every query it makes
// is thread-safe (the courses and lakes of the Hydrology are computed once on demand, under their
// locks).
class MapWorldSource final : public MapSampler {
public:
    // The world of tiles, which must outlive the source.
    explicit MapWorldSource(const TerrainTiles& tiles, const MapSourceParams& params = {});
    // A heightfield (with or without a hydrology) and an ecology, which must outlive the source.
    MapWorldSource(const Heightfield& field, const Ecology& ecology,
                   const MapSourceParams& params = {});

    // Where a map of this many meters per pixel is read.
    [[nodiscard]] MapSourceKind kindFor(double metersPerPixel) const;
    [[nodiscard]] bool hasPlanet() const { return planet_ != nullptr; }
    [[nodiscard]] const MapSourceParams& params() const { return params_; }
    [[nodiscard]] const Heightfield& field() const { return *field_; }
    [[nodiscard]] const Ecology& ecology() const { return *ecology_; }
    [[nodiscard]] const PlanetLevel* planet() const { return planet_; }
    [[nodiscard]] const Hydrology* hydrology() const { return hydrology_; }

    // Stops between two rows of each of its stages, and between two courses of rivers, once
    // cancel is raised.
    void sample(const MapFrame& frame, std::span<MapSample> out, int threads,
                MapSampleStats& stats, MapCancel cancel) const override;

private:
    // The floors of the seas (agent 51, seas.hpp) under the sea of the relief alone and of the
    // planet level (FineRelief reads them from the field).
    void addSeaFloors(const MapFrame& frame, std::span<MapSample> out, int threads,
                      MapCancel cancel) const;
    void sampleRelief(const MapFrame& frame, const Heightfield& field, std::span<MapSample> out,
                      int threads, MapSampleStats& stats, MapCancel cancel) const;
    void samplePlanet(const MapFrame& frame, std::span<MapSample> out, int threads,
                      MapSampleStats& stats, MapCancel cancel) const;
    [[nodiscard]] int planetSubPoints(const MapFrame& frame) const;
    [[nodiscard]] MapSubPoint planetPoint(double x, double z) const;
    void addBiomes(const MapFrame& frame, std::span<const float> continentalness,
                   std::span<MapSample> out, int threads, MapCancel cancel,
                   std::span<const MapSubPoint> points, int n) const;
    void addPlanetWater(const MapFrame& frame, std::span<MapSample> out, int threads,
                        MapCancel cancel) const;

    MapSourceParams params_;
    const Heightfield* field_ = nullptr;  // with its water
    const Heightfield* relief_ = nullptr; // without (the relief the hydrology was built over)
    const Ecology* ecology_ = nullptr;
    const Hydrology* hydrology_ = nullptr;
    const PlanetLevel* planet_ = nullptr;
};

} // namespace terrain
