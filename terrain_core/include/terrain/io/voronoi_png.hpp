#pragma once

#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/voronoi.hpp"
#include "terrain/io/png.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

namespace terrain {

using PlateColor = std::array<std::uint8_t, 3>;

// Fill of each plate kind on the plate map, lightly tinted per plate so that neighboring
// plates of the same kind stay distinct.
inline constexpr PlateColor kContinentColor = {196, 170, 112};
inline constexpr PlateColor kOceanColor = {30, 60, 130};
inline constexpr PlateColor kInlandSeaColor = {40, 125, 160};
inline constexpr PlateColor kShelfColor = {110, 170, 205};

// Boundary lines, by kind, and the plate coastline.
inline constexpr PlateColor kCollisionColor = {230, 30, 30};
inline constexpr PlateColor kDivergenceColor = {0, 230, 230};
inline constexpr PlateColor kTransformColor = {240, 220, 40};
inline constexpr PlateColor kPlateCoastColor = {0, 0, 0};

[[nodiscard]] PlateColor plateKindColor(PlateKind kind);
[[nodiscard]] PlateColor boundaryKindColor(BoundaryKind kind);

// Plate map of a region, RGB: each plate filled with the color of its kind, collision belts
// tinted red and rift troughs tinted cyan, then the plate coastline (where the plate
// continentalness changes sign) in black and the boundaries, 1 px wide, in the color of
// their kind. Pixels sample the world as in sampleHeightGrid.
[[nodiscard]] Image renderPlatesMap(const TectonicPlates& plates, const HeightmapRegion& region);

// Region covering one period of the plate layout, [0, planetSize) along x and z, in size px.
[[nodiscard]] HeightmapRegion planetRegion(const TectonicPlates& plates, int size);

// The whole planet, RGB: the hypsometric preview of field over planetRegion, with the plate
// boundaries drawn 1 px wide in the color of their kind. Requires field.plates().
[[nodiscard]] Image renderWorldMap(const Heightfield& field, int size);

// Writes into directory, creating it if needed:
// - voronoi_plates.png (renderPlatesMap), voronoi_heightmap.png (gray) and
//   voronoi_preview.png (hypsometric) of region;
// - voronoi_world.png: renderWorldMap(field, worldSize), the whole planet.
// Returns false if field has no plates (plates.amplitude <= 0), if worldSize < 2, or if a
// file cannot be written.
[[nodiscard]] bool exportVoronoiPngs(const Heightfield& field, const HeightmapRegion& region,
                                     int worldSize, const std::filesystem::path& directory);

} // namespace terrain
