#include "planet_png_draw.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/io/settlement_png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace terrain {

namespace {

using planet_png::Rgb;

// A color of a hue in [0, 1) at a saturation and a value (HSV).
Rgb hsv(double hue, double saturation, double value) {
    const double h = 6.0 * hue;
    const int sector = std::min(5, static_cast<int>(h));
    const double f = h - sector;
    const auto v = static_cast<float>(255.0 * value);
    const auto p = static_cast<float>(255.0 * value * (1.0 - saturation));
    const auto q = static_cast<float>(255.0 * value * (1.0 - saturation * f));
    const auto t = static_cast<float>(255.0 * value * (1.0 - saturation * (1.0 - f)));
    const std::array<Rgb, 6> colors = {
        {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}}};
    return colors[static_cast<std::size_t>(sector)];
}

// The color of a state: its own hue, saturated for a major kingdom, paler for a minor state.
Rgb stateColor(std::size_t state, settlement::StateRank rank) {
    const double hue = static_cast<double>(noise::deriveSeed(state, 0x6B696E67) >> 11) * 0x1.0p-53;
    return rank == settlement::StateRank::Major ? hsv(hue, 0.75, 0.85) : hsv(hue, 0.35, 0.97);
}

// The pixel of a cell.
std::size_t pixelOf(const PlanetGrid& grid, const Image& image, int scale, std::size_t cell) {
    const int x = std::min(image.width - 1, grid.column(cell) / scale);
    const int y = std::min(image.height - 1, grid.row(cell) / scale);
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
           static_cast<std::size_t>(x);
}

// The land border edges of each pixel, by kind: river, ridge, coast, hostile land, other.
std::vector<std::array<std::uint16_t, 5>> borderPixels(const settlement::ExpansionTerrain& t,
                                                        const settlement::Kingdoms& k,
                                                        const settlement::BorderRule& rule,
                                                        const Image& image, int scale) {
    std::vector<std::array<std::uint16_t, 5>> counts(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height),
        std::array<std::uint16_t, 5>{});
    const PlanetGrid& g = t.grid;
    t.land->forEachLand([&](std::size_t cell, std::size_t a) {
        for (const bool east : {true, false}) {
            const std::size_t n = east ? g.index(g.wrapColumn(g.column(cell) + 1), g.row(cell))
                                       : g.index(g.column(cell), g.wrapRow(g.row(cell) + 1));
            if (!t.land->land(n)) {
                continue;
            }
            const std::size_t b = t.land->slot(n);
            if (k.owner[a] == k.owner[b]) {
                continue;
            }
            const std::uint8_t kinds = settlement::edgeKinds(t, a, b, east, rule);
            const int kind = (kinds & settlement::kAlongRiver) != 0     ? 0
                             : (kinds & settlement::kAlongRidge) != 0   ? 1
                             : (kinds & settlement::kAlongCoast) != 0   ? 2
                             : (kinds & settlement::kAlongHostile) != 0 ? 3
                                                                        : 4;
            std::uint16_t& c =
                counts[pixelOf(g, image, scale, cell)][static_cast<std::size_t>(kind)];
            c = static_cast<std::uint16_t>(std::min(c + 1, 0xFFFF));
        }
    });
    return counts;
}

void square(Image& image, int cx, int cy, int half, Rgb inside) {
    for (int dy = -half - 1; dy <= half + 1; ++dy) {
        for (int dx = -half - 1; dx <= half + 1; ++dx) {
            const bool edge = std::max(std::abs(dx), std::abs(dy)) == half + 1;
            const int x = ((cx + dx) % image.width + image.width) % image.width;
            const int y = ((cy + dy) % image.height + image.height) % image.height;
            planet_png::put(image, x, y, edge ? Rgb{20.0f, 20.0f, 20.0f} : inside);
        }
    }
}

} // namespace

Image renderKingdoms(const PlanetLevel& level, const settlement::ExpansionTerrain& terrain,
                     const settlement::Kingdoms& kingdoms, const settlement::BorderRule& rule,
                     int scale) {
    const PlanetGrid& grid = level.grid();
    Image image = planet_png::grayRelief(level, scale);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t cell = planet_png::middleCell(grid, scale, x, y);
            if (!terrain.land->land(cell)) {
                continue;
            }
            const std::uint16_t owner = kingdoms.owner[terrain.land->slot(cell)];
            if (owner != 0) {
                const std::size_t s = owner - 1u;
                planet_png::put(image, x, y, stateColor(s, kingdoms.states[s].rank));
            }
        }
    }
    constexpr std::array<Rgb, 5> kBorder = {{{40.0f, 110.0f, 255.0f},
                                             {255.0f, 255.0f, 255.0f},
                                             {0.0f, 230.0f, 230.0f},
                                             {255.0f, 150.0f, 0.0f},
                                             {110.0f, 0.0f, 0.0f}}};
    const auto counts = borderPixels(terrain, kingdoms, rule, image, scale);
    for (std::size_t p = 0; p < counts.size(); ++p) {
        const auto& c = counts[p];
        const auto most = std::max_element(c.begin(), c.end());
        if (*most > 0) {
            planet_png::put(image, static_cast<int>(p % static_cast<std::size_t>(image.width)),
                            static_cast<int>(p / static_cast<std::size_t>(image.width)),
                            kBorder[static_cast<std::size_t>(most - c.begin())]);
        }
    }
    for (std::size_t s = 0; s < kingdoms.states.size(); ++s) {
        const std::size_t p = pixelOf(grid, image, scale, kingdoms.capitals.capitals[s].cell);
        const bool major = kingdoms.states[s].rank == settlement::StateRank::Major;
        square(image, static_cast<int>(p % static_cast<std::size_t>(image.width)),
               static_cast<int>(p / static_cast<std::size_t>(image.width)), major ? 2 : 0,
               {255.0f, 255.0f, 255.0f});
    }
    return image;
}

} // namespace terrain
