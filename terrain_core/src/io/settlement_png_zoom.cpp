#include "planet_png_draw.hpp"
#include "settlement_png_colors.hpp"
#include "terrain/io/settlement_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain {

namespace {

using planet_png::Rgb;

constexpr Rgb kSea{15.0f, 20.0f, 40.0f};
constexpr Rgb kRiver{40.0f, 110.0f, 255.0f};
constexpr Rgb kLake{0.0f, 190.0f, 255.0f};
constexpr Rgb kRidge{255.0f, 255.0f, 255.0f};
constexpr Rgb kSlopeRidge{255.0f, 120.0f, 170.0f};
constexpr Rgb kMountain{150.0f, 105.0f, 70.0f};
constexpr Rgb kHostile{255.0f, 170.0f, 60.0f};
constexpr std::uint8_t kRidgeBits = settlement::kRidgeEastBit | settlement::kRidgeSouthBit;

std::size_t cellOf(const PlanetGrid& g, const PlanetWindow& w, int x, int y) {
    return g.index(g.wrapColumn(w.i0 + x), g.wrapRow(w.j0 + y));
}

std::size_t neighbor(const PlanetGrid& g, std::size_t cell, int dx, int dz) {
    return g.index(g.wrapColumn(g.column(cell) + dx), g.wrapRow(g.row(cell) + dz));
}

// The relief of the window in grays, shaded from the north-west; the sea dark.
Image relief(const PlanetLevel& level, const settlement::ExpansionTerrain& t,
             const PlanetWindow& w) {
    const PlanetGrid& g = level.grid();
    Image image{w.width, w.height, 3, {}};
    image.pixels.assign(
        static_cast<std::size_t>(w.width) * static_cast<std::size_t>(w.height) * 3, 0);
    for (int y = 0; y < w.height; ++y) {
        for (int x = 0; x < w.width; ++x) {
            const std::size_t cell = cellOf(g, w, x, y);
            if (!t.land->land(cell)) {
                planet_png::put(image, x, y, kSea);
                continue;
            }
            const double e = level.elevation(cell);
            const double slope = (level.elevation(neighbor(g, cell, -1, 0)) -
                                  level.elevation(neighbor(g, cell, 1, 0)) +
                                  level.elevation(neighbor(g, cell, 0, -1)) -
                                  level.elevation(neighbor(g, cell, 0, 1))) /
                                 (2.0 * g.cellSize);
            const double shade = std::clamp(1.0 + 6.0 * slope, 0.55, 1.45);
            const auto gray =
                static_cast<float>(std::clamp((70.0 + 150.0 * e / 4500.0) * shade, 0.0, 255.0));
            planet_png::put(image, x, y, {gray, gray, gray});
        }
    }
    return image;
}

Rgb pixel(const Image& image, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) * 3;
    return {static_cast<float>(image.pixels[at]), static_cast<float>(image.pixels[at + 1]),
            static_cast<float>(image.pixels[at + 2])};
}

void tint(Image& image, int x, int y, Rgb color, float share) {
    planet_png::put(image, x, y, planet_png::mix(pixel(image, x, y), color, share));
}

// The rivers, the lakes, the mountains and the hostile land of a land cell over the relief.
void waterAndLand(Image& image, const settlement::ExpansionTerrain& t, std::size_t slot, int x,
                  int y) {
    if ((t.mask[slot] & settlement::kLakeBit) != 0) {
        planet_png::put(image, x, y, kLake);
    } else if (t.riverClass(slot) > 0) {
        const auto brightness = 0.7f + 0.1f * static_cast<float>(t.riverClass(slot));
        planet_png::put(image, x, y, planet_png::scaled(kRiver, brightness));
    } else if (t.hostile[slot] != 0) {
        tint(image, x, y, kHostile, 0.35f);
    } else if ((t.mask[slot] & settlement::kMountainBit) != 0) {
        tint(image, x, y, kMountain, 0.35f);
    }
}

// The color of the ridge edges of a land cell: white on a crest line, pink on a slope.
Rgb ridgeColor(const PlanetLevel& level, const settlement::ExpansionTerrain& t, std::size_t cell,
               std::size_t slot) {
    bool crest = false;
    for (const bool east : {true, false}) {
        const std::uint8_t bit = east ? settlement::kRidgeEastBit : settlement::kRidgeSouthBit;
        crest = crest || ((t.mask[slot] & bit) != 0 && crestEdge(level, cell, east));
    }
    return crest ? kRidge : kSlopeRidge;
}

} // namespace

bool crestEdge(const PlanetLevel& level, std::size_t cell, bool east) {
    const PlanetGrid& g = level.grid();
    const int dx = east ? 1 : 0;
    const int dz = east ? 0 : 1;
    const std::size_t before = neighbor(g, cell, -dx, -dz);
    const std::size_t next = neighbor(g, cell, dx, dz);
    const std::size_t after = neighbor(g, cell, 2 * dx, 2 * dz);
    return level.elevation(before) < level.elevation(cell) &&
           level.elevation(after) < level.elevation(next);
}

PlanetWindow planetWindow(const PlanetGrid& grid, double x, double z, double widthKm,
                          double heightKm) {
    const std::size_t center = planetCellAt(grid, x, z);
    PlanetWindow w;
    const auto cells = [&](double km, int most) {
        return std::clamp(static_cast<int>(std::lround(km * 1000.0 / grid.cellSize)), 1, most);
    };
    w.width = cells(widthKm, grid.nx);
    w.height = cells(heightKm, grid.nz);
    w.i0 = grid.wrapColumn(grid.column(center) - w.width / 2);
    w.j0 = grid.wrapRow(grid.row(center) - w.height / 2);
    return w;
}

Image renderRidgesZoom(const PlanetLevel& level, const settlement::ExpansionTerrain& terrain,
                       const PlanetWindow& window) {
    Image image = relief(level, terrain, window);
    const PlanetGrid& g = level.grid();
    for (int y = 0; y < window.height; ++y) {
        for (int x = 0; x < window.width; ++x) {
            const std::size_t cell = cellOf(g, window, x, y);
            if (!terrain.land->land(cell)) {
                continue;
            }
            const std::size_t slot = terrain.land->slot(cell);
            waterAndLand(image, terrain, slot, x, y);
            if ((terrain.mask[slot] & kRidgeBits) != 0) {
                planet_png::put(image, x, y, ridgeColor(level, terrain, cell, slot));
            }
        }
    }
    return image;
}

Image renderCompartmentsZoom(const PlanetLevel& level, const settlement::ExpansionTerrain& terrain,
                             const PlanetWindow& window) {
    Image image = relief(level, terrain, window);
    const PlanetGrid& g = level.grid();
    for (int y = 0; y < window.height; ++y) {
        for (int x = 0; x < window.width; ++x) {
            const std::size_t cell = cellOf(g, window, x, y);
            if (!terrain.land->land(cell)) {
                continue;
            }
            const std::size_t slot = terrain.land->slot(cell);
            const std::uint32_t id = terrain.compartments.of(slot);
            if (id == settlement::kNoCompartment) {
                waterAndLand(image, terrain, slot, x, y);
            } else {
                const double hue = settlement_png::hueOf(id, 0x636F6D70);
                tint(image, x, y, settlement_png::hsv(hue, 0.6, 0.9), 0.75f);
            }
            if ((terrain.mask[slot] & kRidgeBits) != 0) {
                planet_png::put(image, x, y, kRidge);
            }
        }
    }
    return image;
}

Image renderKingdomsZoom(const PlanetLevel& level, const settlement::ExpansionTerrain& terrain,
                         const settlement::Kingdoms& kingdoms, const settlement::BorderRule& rule,
                         const PlanetWindow& window) {
    Image image = relief(level, terrain, window);
    const PlanetGrid& g = level.grid();
    for (int y = 0; y < window.height; ++y) {
        for (int x = 0; x < window.width; ++x) {
            const std::size_t cell = cellOf(g, window, x, y);
            if (!terrain.land->land(cell)) {
                continue;
            }
            const std::size_t a = terrain.land->slot(cell);
            const std::uint16_t owner = kingdoms.owner[a];
            if (owner != 0) {
                const std::size_t s = owner - 1u;
                tint(image, x, y, settlement_png::stateColor(s, kingdoms.states[s].rank), 0.8f);
            }
            for (const bool east : {true, false}) {
                const std::size_t n = neighbor(g, cell, east ? 1 : 0, east ? 0 : 1);
                if (!terrain.land->land(n) || kingdoms.owner[terrain.land->slot(n)] == owner) {
                    continue;
                }
                const std::uint8_t kinds =
                    settlement::edgeKinds(terrain, a, terrain.land->slot(n), east, rule);
                planet_png::put(image, x, y,
                                settlement_png::kBorderColors[settlement_png::borderKind(kinds)]);
            }
        }
    }
    for (std::size_t s = 0; s < kingdoms.states.size(); ++s) {
        const std::size_t cell = kingdoms.capitals.capitals[s].cell;
        const int x = g.wrapColumn(g.column(cell) - window.i0);
        const int y = g.wrapRow(g.row(cell) - window.j0);
        if (x < window.width && y < window.height) {
            settlement_png::capitalSquare(
                image, x, y, kingdoms.states[s].rank == settlement::StateRank::Major ? 3 : 1);
        }
    }
    return image;
}

} // namespace terrain
