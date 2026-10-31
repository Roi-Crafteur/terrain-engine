#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/water_kind.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The layers of the dev map: each one a function of a point of the world to a color and its
// alpha, registered by name. The renderer (map_render.hpp) composites the enabled ones in their
// order of registration, so another module adds a layer (the kingdoms of the settlement, the
// kinds of coast...) by registering a function, without any change to the renderer.
namespace terrain {

// 8-bit color with straight (not premultiplied) alpha.
struct MapColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    friend bool operator==(const MapColor&, const MapColor&) = default;
};

// Where the sample of a pixel was read (MapSampler): the fine relief with its water, the relief
// alone with the water of the planet level, or the cells of the planet level.
enum class MapSourceKind : std::uint8_t { Synthetic, FineRelief, Relief, PlanetCells };

// What the map knows of the world at a pixel.
struct MapSample {
    float ground = 0.0f;               // top of the ground, meters
    WaterKind water = WaterKind::None; // the water over it
    float waterLevel = kNoWaterLevel;  // its surface where there is water
    Biome biome = Biome::Ocean;
    float discharge = 0.0f;    // m^3/s of the river drawn over the pixel, 0 for none
    float riverCover = 0.0f;   // [0, 1]: how much of the pixel its drawn course covers
    float fallDrop = 0.0f;     // meters: the drop of the fall whose mark covers the pixel
    std::uint8_t fallMark = 0; // 0: no mark, kMapFallRing or kMapFallCenter
    MapSourceKind source = MapSourceKind::Synthetic;

    friend bool operator==(const MapSample&, const MapSample&) = default;
};

inline constexpr std::uint8_t kMapFallRing = 1;
inline constexpr std::uint8_t kMapFallCenter = 2;

// A pixel as a layer sees it: its canonical center (x, z), the spacing of the pixels along x and
// z, its sample and the ground of the 4 neighbor pixels (east +x, west, south +z, north).
struct MapPoint {
    double x = 0.0;
    double z = 0.0;
    double stepX = 1.0;
    double stepZ = 1.0;
    const MapSample* sample = nullptr; // never null in a layer
    std::array<float, 4> neighbors{};  // ground east, west, south, north

    [[nodiscard]] const MapSample& at() const { return *sample; }
};

inline constexpr int kMapEast = 0;
inline constexpr int kMapWest = 1;
inline constexpr int kMapSouth = 2;
inline constexpr int kMapNorth = 3;

// A layer: the color it lays over a pixel, alpha 0 where it lays nothing. Called from several
// threads at once: it must only read.
using MapLayerFn = std::function<MapColor(const MapPoint&)>;

// The layers a map may draw, by name, in their order.
class MapLayers {
public:
    // Appends a layer, or replaces in place the layer of the same name. Throws
    // std::invalid_argument for an empty name or an empty function.
    void add(std::string name, MapLayerFn layer);
    // Removes a layer; false if there is none of this name.
    bool remove(std::string_view name);
    // The layer of this name, or nullptr.
    [[nodiscard]] const MapLayerFn* find(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] std::size_t size() const { return layers_.size(); }

    // The layers of these names, in the order of the registry (not of names). Throws
    // std::invalid_argument for a name that is not registered.
    [[nodiscard]] std::vector<const MapLayerFn*> enabled(const std::vector<std::string>& names) const;

private:
    std::vector<std::pair<std::string, MapLayerFn>> layers_;
};

// above laid over below ("over" with straight alpha), in integers: the same bytes everywhere.
[[nodiscard]] MapColor over(MapColor below, MapColor above);

} // namespace terrain
