#include "terrain/io/settlement_png.hpp"

#include "planet_png_draw.hpp"
#include "terrain/io/planet_png.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace terrain {

namespace {

using planet_png::ColorStop;
using planet_png::Rgb;

constexpr std::array<ColorStop, 3> kFertility = {
    {{0.0f, {120.0f, 85.0f, 55.0f}}, {0.5f, {225.0f, 205.0f, 90.0f}}, {1.0f, {40.0f, 150.0f, 50.0f}}}};
constexpr std::array<ColorStop, 5> kHabitability = {{{0.0f, {40.0f, 20.0f, 60.0f}},
                                                     {0.25f, {120.0f, 30.0f, 90.0f}},
                                                     {0.5f, {200.0f, 70.0f, 50.0f}},
                                                     {0.75f, {240.0f, 160.0f, 40.0f}},
                                                     {1.0f, {255.0f, 250.0f, 180.0f}}}};
constexpr std::array<ColorStop, 3> kPrana = {
    {{0.0f, {120.0f, 110.0f, 100.0f}}, {0.5f, {150.0f, 210.0f, 120.0f}}, {1.0f, {10.0f, 120.0f, 70.0f}}}};
constexpr std::array<ColorStop, 4> kMana = {{{0.0f, {20.0f, 25.0f, 60.0f}},
                                             {0.4f, {40.0f, 90.0f, 200.0f}},
                                             {0.7f, {150.0f, 60.0f, 210.0f}},
                                             {1.0f, {255.0f, 190.0f, 255.0f}}}};
constexpr std::array<ColorStop, 3> kDanger = {
    {{0.0f, {235.0f, 230.0f, 210.0f}}, {0.5f, {240.0f, 140.0f, 40.0f}}, {1.0f, {120.0f, 10.0f, 10.0f}}}};
constexpr std::array<ColorStop, 3> kResource = {
    {{0.0f, {70.0f, 70.0f, 75.0f}}, {0.4f, {170.0f, 120.0f, 40.0f}}, {1.0f, {255.0f, 215.0f, 60.0f}}}};
constexpr std::array<ColorStop, 2> kRoughness = {
    {{0.0f, {240.0f, 235.0f, 220.0f}}, {1.0f, {90.0f, 50.0f, 25.0f}}}};
constexpr std::array<ColorStop, 3> kDistance = {
    {{0.0f, {20.0f, 80.0f, 200.0f}}, {30.0f, {120.0f, 190.0f, 240.0f}}, {100.0f, {250.0f, 250.0f, 250.0f}}}};

std::span<const ColorStop> stopsOf(SettlementPalette palette) {
    switch (palette) {
    case SettlementPalette::Fertility:
        return kFertility;
    case SettlementPalette::Habitability:
        return kHabitability;
    case SettlementPalette::Prana:
        return kPrana;
    case SettlementPalette::Mana:
        return kMana;
    case SettlementPalette::Danger:
        return kDanger;
    case SettlementPalette::Resource:
        return kResource;
    case SettlementPalette::Roughness:
        return kRoughness;
    case SettlementPalette::Distance:
        return kDistance;
    }
    return kFertility;
}

// The pixel of the point (x, z) of the world, wrapped.
bool pixelOf(const PlanetGrid& grid, int scale, const Image& image, double x, double z, int& px,
             int& py) {
    const double u = (wrapCoordinate(x, grid.periodX()) + 0.5 * grid.periodX()) / grid.cellSize;
    const double v = (wrapCoordinate(z, grid.periodZ()) + 0.5 * grid.periodZ()) / grid.cellSize;
    px = std::min(image.width - 1, static_cast<int>(u) / scale);
    py = std::min(image.height - 1, static_cast<int>(v) / scale);
    return px >= 0 && py >= 0;
}

void disc(Image& image, int cx, int cy, int radius, Rgb color, bool ring) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int d2 = dx * dx + dy * dy;
            const bool inside = d2 <= radius * radius;
            const bool edge = inside && d2 > (radius - 1) * (radius - 1);
            if (ring ? edge : inside) {
                const int x = ((cx + dx) % image.width + image.width) % image.width;
                const int y = ((cy + dy) % image.height + image.height) % image.height;
                planet_png::put(image, x, y, color);
            }
        }
    }
}

int pixels(const PlanetGrid& grid, int scale, double km, int least) {
    return std::max(least, static_cast<int>(km * 1000.0 / (grid.cellSize * scale) + 0.5));
}

Rgb pixelColor(const Image& image, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) * 3;
    return {static_cast<float>(image.pixels[at]), static_cast<float>(image.pixels[at + 1]),
            static_cast<float>(image.pixels[at + 2])};
}

// One color per terrain model: the known ones fixed, the others from their index.
Rgb modelColor(const std::string& id, std::size_t index) {
    struct Named {
        const char* id;
        Rgb color;
    };
    constexpr std::array<Named, 8> kNamed = {{{"glacier", {245.0f, 250.0f, 255.0f}},
                                              {"mountain", {130.0f, 110.0f, 95.0f}},
                                              {"swamp", {95.0f, 120.0f, 60.0f}},
                                              {"desert", {235.0f, 205.0f, 140.0f}},
                                              {"tundra", {170.0f, 200.0f, 195.0f}},
                                              {"hills", {190.0f, 160.0f, 100.0f}},
                                              {"forest", {35.0f, 105.0f, 45.0f}},
                                              {"plains", {150.0f, 200.0f, 100.0f}}}};
    for (const Named& n : kNamed) {
        if (id == n.id) {
            return n.color;
        }
    }
    const auto k = static_cast<float>(index);
    return {std::fmod(70.0f * k, 255.0f), std::fmod(150.0f + 40.0f * k, 255.0f), 200.0f};
}

} // namespace

Image renderSettlementMap(const PlanetLevel& level, const settlement::SettlementMaps& maps,
                          const planet::LandValues<std::uint8_t>& values,
                          SettlementPalette palette, int scale) {
    const PlanetGrid& grid = level.grid();
    Image image = planet_png::grayRelief(level, scale);
    const std::span<const ColorStop> stops = stopsOf(palette);
    const bool distance = palette == SettlementPalette::Distance;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const planet_png::Block b = planet_png::block(grid, scale, x, y);
            double sum = 0.0;
            int land = 0;
            int all = 0;
            for (int j = b.j0; j < b.j1; ++j) {
                for (int i = b.i0; i < b.i1; ++i) {
                    const std::size_t cell = grid.index(i, j);
                    ++all;
                    if (maps.land->land(cell)) {
                        sum += values[cell];
                        ++land;
                    }
                }
            }
            if (land == 0) {
                continue;
            }
            const auto mean = static_cast<float>(sum / land);
            const Rgb color = planet_png::ramp(stops, distance ? mean : mean / 255.0f);
            const float share = static_cast<float>(land) / static_cast<float>(all);
            planet_png::put(image, x, y, planet_png::mix(pixelColor(image, x, y), color, share));
        }
    }
    return image;
}

Image renderTerrainModels(const PlanetLevel& level, const settlement::SettlementMaps& maps,
                          const settlement::SettlementConfig& config, int scale) {
    const PlanetGrid& grid = level.grid();
    Image image = planet_png::grayRelief(level, scale);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t cell = planet_png::middleCell(grid, scale, x, y);
            if (!maps.land->land(cell)) {
                continue;
            }
            const std::size_t m = maps.terrainModel[cell];
            const std::string& id = m < config.terrainModels.size() ? config.terrainModels[m].id : "";
            planet_png::put(image, x, y, modelColor(id, m));
        }
    }
    return image;
}

Image renderManaNetwork(const PlanetLevel& level, const settlement::SettlementMaps& maps,
                        int scale) {
    const PlanetGrid& grid = level.grid();
    Image image = renderSettlementMap(level, maps, maps.mana, SettlementPalette::Mana, scale);
    int px = 0;
    int py = 0;
    for (const settlement::TelluricLine& l : maps.network.lines) {
        const Rgb color = l.major ? Rgb{255.0f, 255.0f, 255.0f} : Rgb{150.0f, 190.0f, 255.0f};
        for (std::size_t p = 0; p < l.x.size(); ++p) {
            if (pixelOf(grid, scale, image, l.x[p], l.z[p], px, py)) {
                planet_png::put(image, px, py, color);
            }
        }
    }
    for (const settlement::DeadZone& d : maps.network.deadZones) {
        if (pixelOf(grid, scale, image, d.x, d.z, px, py)) {
            disc(image, px, py, pixels(grid, scale, d.radiusKm, 2), {0.0f, 0.0f, 0.0f}, true);
        }
    }
    for (const settlement::ArcaneCrater& c : maps.geology.craters) {
        if (pixelOf(grid, scale, image, c.x, c.z, px, py)) {
            disc(image, px, py, pixels(grid, scale, c.radiusKm, 2), {255.0f, 40.0f, 200.0f}, true);
        }
    }
    for (const settlement::Volcano& v : maps.geology.volcanoes) {
        if (pixelOf(grid, scale, image, v.x, v.z, px, py)) {
            planet_png::put(image, px, py, v.active ? Rgb{255.0f, 60.0f, 20.0f}
                                                    : Rgb{140.0f, 30.0f, 20.0f});
        }
    }
    for (const settlement::ManaNode& n : maps.network.nodes) {
        if (pixelOf(grid, scale, image, n.x, n.z, px, py)) {
            const bool visible = n.known == settlement::Known::Visible;
            disc(image, px, py, visible ? 3 : 2,
                 visible ? Rgb{255.0f, 225.0f, 60.0f} : Rgb{255.0f, 150.0f, 40.0f}, false);
        }
    }
    return image;
}

bool exportSettlementMapPngs(const PlanetLevel& level, const settlement::SettlementMaps& maps,
                             const settlement::SettlementData& data,
                             const std::filesystem::path& directory, int maxWidth) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const int scale = planetImageScale(level.grid(), maxWidth);
    const auto map = [&](const char* name, const planet::LandValues<std::uint8_t>& values,
                         SettlementPalette palette) {
        return writePng(directory / (std::string("settlement_") + name + ".png"),
                        renderSettlementMap(level, maps, values, palette, scale));
    };
    bool ok = map("roughness", maps.roughness, SettlementPalette::Roughness) &&
              map("water_km", maps.waterKm, SettlementPalette::Distance) &&
              map("arable", maps.arable, SettlementPalette::Fertility) &&
              map("food", maps.food, SettlementPalette::Fertility) &&
              map("danger", maps.danger, SettlementPalette::Danger) &&
              map("prana", maps.prana, SettlementPalette::Prana) &&
              map("mana", maps.mana, SettlementPalette::Mana) &&
              map("habitability", maps.habitability, SettlementPalette::Habitability);
    ok = ok && writePng(directory / "settlement_terrain_models.png",
                        renderTerrainModels(level, maps, data.config, scale));
    ok = ok && writePng(directory / "settlement_mana_network.png",
                        renderManaNetwork(level, maps, scale));
    for (std::size_t r = 0; ok && r < data.resources.size(); ++r) {
        ok = writePng(directory / ("resource_" + data.resources[r].id + ".png"),
                      renderSettlementMap(level, maps, maps.resources[r],
                                          SettlementPalette::Resource, scale));
    }
    return ok;
}

} // namespace terrain
