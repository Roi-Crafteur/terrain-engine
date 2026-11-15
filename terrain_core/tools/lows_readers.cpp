// What the readers of the world make of the dry land below the sea level (lows_readers.hpp).
#include "lows_readers.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lows_tool {

namespace {

using terrain::Biome;
using terrain::HeightSample;
using terrain::WaterKind;

constexpr double kSlopeStep = 8.0; // as Terrain.Info (Bridge/point_info.cpp)
// The continentalness of the Heightfield is the coast distance over this scale.
constexpr double kContinentalScale = 1'500'000.0;
// A river point is held at the sea level when its water lies within kHeldLevel of 0 m over ground
// more than kHeldDepth below it, kHeldInland from the sea at least.
constexpr float kHeldLevel = 0.01f;
constexpr float kHeldDepth = 1.0f;
constexpr double kHeldInland = 20'000.0;
// The reaches looked for within kReachRadius of the centers of the dry lows.
constexpr double kReachRadius = 150'000.0;

// The column as the biomes read it (Terrain.Info: Bridge/point_info.cpp siteOf), its true water.
terrain::SurfaceSite siteOf(const terrain::Heightfield& field, double x, double z,
                            const HeightSample& s) {
    terrain::SurfaceSite site;
    site.ground = s.height;
    site.continentalness = s.continentalness;
    const std::array<float, 4> h = {field.heightAt(x + kSlopeStep, z),
                                    field.heightAt(x - kSlopeStep, z),
                                    field.heightAt(x, z + kSlopeStep),
                                    field.heightAt(x, z - kSlopeStep)};
    const double run = 2.0 * kSlopeStep;
    site.gradX = static_cast<float>((static_cast<double>(h[0]) - h[1]) / run);
    site.gradZ = static_cast<float>((static_cast<double>(h[2]) - h[3]) / run);
    float difference = 0.0f;
    for (const float height : h) {
        difference = std::max(difference, std::abs(height - site.ground));
    }
    site.steepness = static_cast<float>(difference / kSlopeStep);
    site.water = s.water;
    site.waterLevel = s.waterLevel;
    site.dunes = s.dunes;
    site.erg = s.erg;
    site.ergFloor = s.ergFloor;
    return site;
}

Biome biomeAt(const terrain::Heightfield& field, const terrain::TerrainTiles& tiles, double x,
              double z) {
    const HeightSample s = field.sampleAt(x, z);
    return tiles.ecology().sampleAt(x, z, siteOf(field, x, z, s)).biome;
}

// At most limit points spread over the list (every n-th).
std::vector<Point> spread(const std::vector<Point>& points, std::size_t limit) {
    std::vector<Point> out;
    if (points.empty() || limit == 0) {
        return out;
    }
    const std::size_t every = std::max<std::size_t>(1, points.size() / limit);
    for (std::size_t k = 0; k < points.size() && out.size() < limit; k += every) {
        out.push_back(points[k]);
    }
    return out;
}

// The surface blend of the tile of LOD 0 over p (its texel nearest p) and its plants.
void probeTile(const terrain::TerrainTiles& tiles, const Point& p, ReaderFigures& out) {
    const terrain::TileKey key = terrain::tileContaining(terrain::worldToChunk(p.x, p.z), 0);
    const terrain::TileData data = tiles.generate({key, 0, 0});
    ++out.tiles;
    if (!data.surfaceBlend.empty()) {
        const double pitch = terrain::kTileBlendStride * terrain::tileCellMeters(0);
        const auto texel = [&](double v, double origin) {
            const auto t = static_cast<int>(std::lround((v - origin) / pitch));
            return std::clamp(t, 0, terrain::kTileBlendTexels - 1);
        };
        const int i = texel(p.x, data.originX);
        const int k = texel(p.z, data.originZ);
        const terrain::SurfaceBlend blend = terrain::unpackBlend(
            data.surfaceBlend[static_cast<std::size_t>(k * terrain::kTileBlendTexels + i)]);
        out.blendOcean += blend.biomes[0] == Biome::Ocean ? 1 : 0;
        out.blendWater += blend.materials[0] == terrain::SurfaceMaterial::Water ? 1 : 0;
    }
    for (const terrain::Plant& plant : data.plants) {
        ++out.plantsTotal;
        out.plantsBelow += plant.y < terrain::kSeaLevel ? 1 : 0;
    }
}

void probeRivers(const terrain::Heightfield& field, const std::vector<Point>& centers,
                 ReaderFigures& out) {
    const terrain::Hydrology* hydrology = field.hydrology();
    if (hydrology == nullptr) {
        return;
    }
    std::vector<std::size_t> reaches;
    for (const Point& c : centers) {
        const std::vector<std::size_t> near = hydrology->reachesNear(c.x, c.z, kReachRadius);
        reaches.insert(reaches.end(), near.begin(), near.end());
    }
    std::sort(reaches.begin(), reaches.end());
    reaches.erase(std::unique(reaches.begin(), reaches.end()), reaches.end());
    for (const std::size_t r : reaches) {
        for (const terrain::RiverPoint& p : hydrology->reach(r).points) {
            ++out.riverPoints;
            if (!(std::abs(p.level - terrain::kSeaLevel) <= kHeldLevel)) {
                continue;
            }
            const HeightSample relief = hydrology->relief().sampleAt(p.x, p.z);
            const double inland = static_cast<double>(relief.continentalness) * kContinentalScale;
            if (relief.height < terrain::kSeaLevel - kHeldDepth && inland > kHeldInland) {
                ++out.riverHeld;
                if (out.heldExamples.size() < 8) {
                    out.heldExamples.push_back({p.x, p.z});
                }
            }
        }
    }
}

} // namespace

ReaderFigures measureReaders(const terrain::Heightfield& field, const terrain::TerrainTiles& tiles,
                             const std::vector<Point>& dryBelow, const std::vector<Point>& lowFar,
                             const std::vector<Point>& lowCenters, std::size_t limit) {
    ReaderFigures out;
    for (const Point& p : spread(dryBelow, limit)) {
        ++out.points;
        const Biome b = biomeAt(field, tiles, p.x, p.z);
        out.biomeOcean += b == Biome::Ocean ? 1 : 0;
        out.biomeBeach += b == Biome::Beach ? 1 : 0;
        out.biomeMarsh += b == Biome::Marsh ? 1 : 0;
        probeTile(tiles, p, out);
    }
    for (const Point& p : spread(lowFar, limit)) {
        ++out.lowPoints;
        const Biome b = biomeAt(field, tiles, p.x, p.z);
        out.lowBeach += b == Biome::Beach ? 1 : 0;
        out.lowMarsh += b == Biome::Marsh ? 1 : 0;
    }
    probeRivers(field, lowCenters, out);
    return out;
}

} // namespace lows_tool
