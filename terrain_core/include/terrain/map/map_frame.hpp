#pragma once

#include "terrain/world_size.hpp"

#include <compare>
#include <cstdint>

// The dev map (agent 35): images of the world seen from above, as data. Nothing here draws: the
// images are bytes that the viewers show (the dev map and the minimap of Unreal, the PNG of
// terrain_export_map). A view of the world, never a part of it: nothing changes a bit of it.
namespace terrain {

// The pixels of the tiles of the map pyramid: level L has 2^L meters per pixel.
inline constexpr int kMapTilePixels = 256;
// 32,768 m per pixel: the whole default world on 1,832 x 1,068 pixels. Every side of a valid
// world is a multiple of 65,536 m, so it holds a whole number of pixels on every level.
inline constexpr int kMapMaxLevel = 15;
// The widest image a frame may have, along each axis.
inline constexpr int kMapMaxPixels = 16384;

// A box of the world, meters: its west edge x, its north edge z (any image: the world wraps
// around), its width along +x (east) and its height along +z (south).
struct MapBox {
    double x = 0.0;
    double z = 0.0;
    double width = 0.0;
    double height = 0.0;

    friend bool operator==(const MapBox&, const MapBox&) = default;
};

// The pixels of an image of a box: width x height pixels, row 0 along the north edge, column 0
// along the west edge. Pixel (i, j) covers [x + i sx, x + (i + 1) sx) x [z + j sz, z + (j + 1) sz)
// with sx = box.width / width and sz = box.height / height, its center half a pixel inside. Every
// position is brought back into the canonical range of the world (wrapCoordinate, exact), so two
// images of the same box shifted by whole worlds are the same pixels.
class MapFrame {
public:
    // Throws std::invalid_argument unless the world is valid, the box has finite coordinates and
    // a positive finite size, and 1 <= width, height <= kMapMaxPixels.
    MapFrame(const MapBox& box, int width, int height, const WorldSize& world = {});

    [[nodiscard]] const MapBox& box() const { return box_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }
    [[nodiscard]] double stepX() const { return stepX_; } // meters per pixel along x
    [[nodiscard]] double stepZ() const { return stepZ_; } // and along z

    // The canonical center of column i and of row j; any integer, also outside the image (the
    // border of the samples, -1 and width or height).
    [[nodiscard]] double pixelX(std::int64_t i) const;
    [[nodiscard]] double pixelZ(std::int64_t j) const;
    // The column and the row whose pixel holds the image of (x, z) nearest to the center of the
    // box: in [0, width) and [0, height) inside the box, outside it otherwise. pixelX(i) gives
    // back column i, pixelZ(j) row j.
    [[nodiscard]] std::int64_t columnAt(double x) const;
    [[nodiscard]] std::int64_t rowAt(double z) const;
    // Meters east of the west edge (south of the north edge) of the image of x (z) nearest to the
    // center of the box: the column of x is floor(offsetX(x) / stepX()).
    [[nodiscard]] double offsetX(double x) const;
    [[nodiscard]] double offsetZ(double z) const;

    friend bool operator==(const MapFrame&, const MapFrame&) = default;

private:
    MapBox box_;
    int width_ = 1;
    int height_ = 1;
    WorldSize world_;
    double stepX_ = 1.0;
    double stepZ_ = 1.0;
};

// A tile of the map pyramid: tx counts the tiles east of the west edge of the canonical range
// (-world.x / 2), tz south of its north edge (-world.z / 2); level L has 2^L meters per pixel.
struct MapTileKey {
    int level = 0;
    std::int64_t tx = 0;
    std::int64_t tz = 0;

    friend auto operator<=>(const MapTileKey&, const MapTileKey&) = default;
};

// 2^level meters. Throws std::invalid_argument unless 0 <= level <= kMapMaxLevel.
[[nodiscard]] double mapLevelMeters(int level);
// The level whose pixels are the smallest at least metersPerPixel meters wide, within
// [0, kMapMaxLevel] (0 for anything up to a meter).
[[nodiscard]] int mapLevelFor(double metersPerPixel);

// The pixels of the whole world along x and along z on a level, and the tiles that hold them
// (the last tile of a row or column reaches past the edge of the world: its pixels beyond it are
// the first pixels again).
[[nodiscard]] std::int64_t mapWorldColumns(int level, const WorldSize& world = {});
[[nodiscard]] std::int64_t mapWorldRows(int level, const WorldSize& world = {});
[[nodiscard]] std::int64_t mapTileColumns(int level, const WorldSize& world = {});
[[nodiscard]] std::int64_t mapTileRows(int level, const WorldSize& world = {});

// The key brought back into [0, mapTileColumns) x [0, mapTileRows).
[[nodiscard]] MapTileKey wrapTile(const MapTileKey& key, const WorldSize& world = {});
// The tile of a level holding the point (x, z), any image, wrapped.
[[nodiscard]] MapTileKey mapTileAt(int level, double x, double z, const WorldSize& world = {});
// The box of a tile (its key as given, not wrapped) and its frame of kMapTilePixels square.
[[nodiscard]] MapBox mapTileBox(const MapTileKey& key, const WorldSize& world = {});
[[nodiscard]] MapFrame mapTileFrame(const MapTileKey& key, const WorldSize& world = {});

} // namespace terrain
