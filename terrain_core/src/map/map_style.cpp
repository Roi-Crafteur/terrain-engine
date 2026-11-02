#include "terrain/map/map_style.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/ecology_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain {

namespace {

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct Stop {
    float height = 0.0f;
    Rgb color;
};

// The tints of io/relief_png.cpp, from the abyssal plains to the highest summits.
constexpr std::array<Stop, 5> kSea = {{
    {-7000.0f, {8.0f, 20.0f, 60.0f}},
    {-4000.0f, {20.0f, 50.0f, 115.0f}},
    {-1500.0f, {40.0f, 95.0f, 165.0f}},
    {-200.0f, {95.0f, 160.0f, 215.0f}},
    {0.0f, {135.0f, 195.0f, 230.0f}},
}};
constexpr std::array<Stop, 10> kLand = {{
    {0.0f, {80.0f, 140.0f, 72.0f}},
    {200.0f, {118.0f, 162.0f, 88.0f}},
    {500.0f, {168.0f, 178.0f, 108.0f}},
    {1000.0f, {198.0f, 170.0f, 112.0f}},
    {1600.0f, {180.0f, 140.0f, 98.0f}},
    {2400.0f, {150.0f, 115.0f, 92.0f}},
    {3200.0f, {135.0f, 120.0f, 112.0f}},
    {4200.0f, {165.0f, 160.0f, 158.0f}},
    {5200.0f, {228.0f, 228.0f, 232.0f}},
    {6500.0f, {255.0f, 255.0f, 255.0f}},
}};
constexpr std::array<Stop, 3> kBelowSea = {{
    {-450.0f, {150.0f, 112.0f, 82.0f}},
    {-100.0f, {196.0f, 168.0f, 128.0f}},
    {0.0f, {222.0f, 208.0f, 168.0f}},
}};

constexpr std::array<double, 9> kContourIntervals = {5.0,   10.0,  20.0,   50.0,  100.0,
                                                     200.0, 500.0, 1000.0, 2000.0};

template <std::size_t N>
Rgb tint(const std::array<Stop, N>& stops, float height) {
    if (!(height > stops.front().height)) {
        return stops.front().color;
    }
    if (height >= stops.back().height) {
        return stops.back().color;
    }
    std::size_t i = 0;
    while (height >= stops[i + 1].height) {
        ++i;
    }
    const Stop& a = stops[i];
    const Stop& b = stops[i + 1];
    const float t = (height - a.height) / (b.height - a.height);
    return {a.color.r + (b.color.r - a.color.r) * t, a.color.g + (b.color.g - a.color.g) * t,
            a.color.b + (b.color.b - a.color.b) * t};
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

MapColor lit(Rgb c, float light, std::uint8_t alpha = 255) {
    return {toByte(c.r * light), toByte(c.g * light), toByte(c.b * light), alpha};
}

bool underStandingWater(const MapSample& s) {
    return s.water == WaterKind::Ocean || s.water == WaterKind::Lake ||
           s.water == WaterKind::SaltLake;
}

// The index of the contour level below height.
double levelOf(float height, double interval) {
    return std::floor(static_cast<double>(height) / interval);
}

} // namespace

double mapReliefExaggeration(double metersPerPixel) {
    return std::clamp(std::sqrt(metersPerPixel / 10.0), 1.0, 30.0);
}

float mapShade(const MapPoint& p) {
    const double e = mapReliefExaggeration(std::max(p.stepX, p.stepZ));
    const double sx = e * (p.neighbors[kMapEast] - p.neighbors[kMapWest]) / (2.0 * p.stepX);
    const double sz = e * (p.neighbors[kMapSouth] - p.neighbors[kMapNorth]) / (2.0 * p.stepZ);
    const double half = std::sqrt(0.5);
    const double lambert = (0.5 * sx + half + 0.5 * sz) / std::sqrt(sx * sx + 1.0 + sz * sz);
    return static_cast<float>(std::clamp(0.3 + 0.7 * lambert / half, 0.2, 1.35));
}

double mapContourInterval(double metersPerPixel) {
    for (const double interval : kContourIntervals) {
        if (interval >= 6.0 * metersPerPixel) {
            return interval;
        }
    }
    return kContourIntervals.back();
}

MapColor mapRiverColor(float discharge) {
    const double q = std::max(static_cast<double>(discharge), 10.0);
    const double t = std::clamp((dmath::log2(q) - dmath::log2(10.0)) /
                                    (dmath::log2(100000.0) - dmath::log2(10.0)),
                                0.0, 1.0);
    const auto mix = [t](float a, float b) {
        return toByte(static_cast<float>(a + (b - a) * t));
    };
    return {mix(95.0f, 20.0f), mix(170.0f, 60.0f), mix(240.0f, 170.0f), 255};
}

MapColor reliefLayer(const MapPoint& p) {
    const MapSample& s = p.at();
    const float light = mapShade(p);
    if (s.water == WaterKind::Ocean) {
        return lit(tint(kSea, s.ground), 0.75f + 0.25f * light);
    }
    if (s.ground < 0.0f) {
        return lit(tint(kBelowSea, s.ground), light);
    }
    return lit(tint(kLand, s.ground), light);
}

MapColor biomeLayer(const MapPoint& p) {
    const MapSample& s = p.at();
    if (s.water == WaterKind::Ocean || s.biome == Biome::Ocean) {
        return {};
    }
    const Rgb8& c = kBiomeRgb[static_cast<std::size_t>(s.biome)];
    return lit({static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])},
               mapShade(p));
}

MapColor waterLayer(const MapPoint& p) {
    const MapSample& s = p.at();
    if (s.fallMark == kMapFallCenter) {
        return {250, 250, 255, 255};
    }
    if (s.fallMark == kMapFallRing) {
        return {205, 30, 40, 255};
    }
    MapColor water;
    if (s.water == WaterKind::Ocean) {
        water = {25, 70, 150, 90};
    } else if (s.water == WaterKind::Lake) {
        water = {45, 105, 190, 235};
    } else if (s.water == WaterKind::SaltLake) {
        water = {170, 205, 210, 235};
    } else if (s.water == WaterKind::River) {
        water = mapRiverColor(s.discharge);
    }
    if (s.riverCover > 0.0f && water.a < 255) {
        MapColor river = mapRiverColor(s.discharge);
        river.a = toByte(255.0f * std::min(s.riverCover, 1.0f));
        water = over(water, river);
    }
    return water;
}

MapColor contourLayer(const MapPoint& p) {
    const MapSample& s = p.at();
    if (underStandingWater(s)) {
        return {};
    }
    const double interval = mapContourInterval(std::max(p.stepX, p.stepZ));
    const double here = levelOf(s.ground, interval);
    const double east = levelOf(p.neighbors[kMapEast], interval);
    const double south = levelOf(p.neighbors[kMapSouth], interval);
    if (here == east && here == south) {
        return {};
    }
    // The level crossed toward the higher neighbor: the line lies on the lower pixel's side.
    const double crossed = std::max({here, east, south});
    const bool major = std::fmod(std::abs(crossed), 5.0) == 0.0;
    return {70, 45, 25, static_cast<std::uint8_t>(major ? 200 : 110)};
}

MapLayers defaultMapLayers() {
    MapLayers layers;
    layers.add(kMapReliefLayer, reliefLayer);
    layers.add(kMapBiomesLayer, biomeLayer);
    layers.add(kMapWaterLayer, waterLayer);
    layers.add(kMapContoursLayer, contourLayer);
    return layers;
}

std::vector<std::string> defaultMapLayerNames() {
    return {kMapReliefLayer, kMapWaterLayer};
}

} // namespace terrain
