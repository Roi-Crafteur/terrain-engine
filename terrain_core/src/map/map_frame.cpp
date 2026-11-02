#include "terrain/map/map_frame.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace terrain {

namespace {

bool finite(double v) {
    return std::isfinite(v);
}

void checkLevel(int level) {
    if (level < 0 || level > kMapMaxLevel) {
        throw std::invalid_argument("map level " + std::to_string(level) + " outside [0, " +
                                    std::to_string(kMapMaxLevel) + "]");
    }
}

// The cell of a periodic axis holding offset, in steps from the start of the box.
std::int64_t cellOf(double offset, double step) {
    return static_cast<std::int64_t>(std::floor(offset / step));
}

// i modulo count, in [0, count).
std::int64_t positiveModulo(std::int64_t i, std::int64_t count) {
    const std::int64_t r = i % count;
    return r < 0 ? r + count : r;
}

} // namespace

MapFrame::MapFrame(const MapBox& box, int width, int height, const WorldSize& world)
    : box_(box), width_(width), height_(height), world_(world) {
    validate(world);
    if (!finite(box.x) || !finite(box.z) || !finite(box.width) || !finite(box.height) ||
        !(box.width > 0.0) || !(box.height > 0.0)) {
        throw std::invalid_argument("map box: finite coordinates and a positive size expected");
    }
    if (width < 1 || height < 1 || width > kMapMaxPixels || height > kMapMaxPixels) {
        throw std::invalid_argument("map frame: 1 to " + std::to_string(kMapMaxPixels) +
                                    " pixels along each axis expected");
    }
    stepX_ = box.width / static_cast<double>(width);
    stepZ_ = box.height / static_cast<double>(height);
}

double MapFrame::pixelX(std::int64_t i) const {
    return wrapCoordinate(box_.x + (static_cast<double>(i) + 0.5) * stepX_, world_.x);
}

double MapFrame::pixelZ(std::int64_t j) const {
    return wrapCoordinate(box_.z + (static_cast<double>(j) + 0.5) * stepZ_, world_.z);
}

double MapFrame::offsetX(double x) const {
    const double half = 0.5 * box_.width;
    return wrapCoordinate(x - (box_.x + half), world_.x) + half;
}

double MapFrame::offsetZ(double z) const {
    const double half = 0.5 * box_.height;
    return wrapCoordinate(z - (box_.z + half), world_.z) + half;
}

std::int64_t MapFrame::columnAt(double x) const {
    return cellOf(offsetX(x), stepX_);
}

std::int64_t MapFrame::rowAt(double z) const {
    return cellOf(offsetZ(z), stepZ_);
}

double mapLevelMeters(int level) {
    checkLevel(level);
    return static_cast<double>(std::int64_t{1} << level);
}

int mapLevelFor(double metersPerPixel) {
    int level = 0;
    while (level < kMapMaxLevel && mapLevelMeters(level) < metersPerPixel) {
        ++level;
    }
    return level;
}

std::int64_t mapWorldColumns(int level, const WorldSize& world) {
    return static_cast<std::int64_t>(world.x / mapLevelMeters(level));
}

std::int64_t mapWorldRows(int level, const WorldSize& world) {
    return static_cast<std::int64_t>(world.z / mapLevelMeters(level));
}

std::int64_t mapTileColumns(int level, const WorldSize& world) {
    return (mapWorldColumns(level, world) + kMapTilePixels - 1) / kMapTilePixels;
}

std::int64_t mapTileRows(int level, const WorldSize& world) {
    return (mapWorldRows(level, world) + kMapTilePixels - 1) / kMapTilePixels;
}

MapTileKey wrapTile(const MapTileKey& key, const WorldSize& world) {
    return {key.level, positiveModulo(key.tx, mapTileColumns(key.level, world)),
            positiveModulo(key.tz, mapTileRows(key.level, world))};
}

MapTileKey mapTileAt(int level, double x, double z, const WorldSize& world) {
    const double meters = mapLevelMeters(level);
    const double cx = wrapCoordinate(x, world.x) + 0.5 * world.x;
    const double cz = wrapCoordinate(z, world.z) + 0.5 * world.z;
    const std::int64_t column = positiveModulo(cellOf(cx, meters), mapWorldColumns(level, world));
    const std::int64_t row = positiveModulo(cellOf(cz, meters), mapWorldRows(level, world));
    return {level, column / kMapTilePixels, row / kMapTilePixels};
}

MapBox mapTileBox(const MapTileKey& key, const WorldSize& world) {
    const double side = mapLevelMeters(key.level) * kMapTilePixels;
    return {-0.5 * world.x + static_cast<double>(key.tx) * side,
            -0.5 * world.z + static_cast<double>(key.tz) * side, side, side};
}

MapFrame mapTileFrame(const MapTileKey& key, const WorldSize& world) {
    return MapFrame(mapTileBox(key, world), kMapTilePixels, kMapTilePixels, world);
}

} // namespace terrain
