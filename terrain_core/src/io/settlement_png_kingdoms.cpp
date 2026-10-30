#include "planet_png_draw.hpp"
#include "settlement_png_colors.hpp"
#include "terrain/io/settlement_png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

namespace {

using planet_png::Rgb;
using settlement_png::borderKind;
using settlement_png::kBorderKinds;
using Counts = std::array<std::uint16_t, kBorderKinds>;

// The pixel of a cell.
std::size_t pixelOf(const PlanetGrid& grid, const Image& image, int scale, std::size_t cell) {
    const int x = std::min(image.width - 1, grid.column(cell) / scale);
    const int y = std::min(image.height - 1, grid.row(cell) / scale);
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
           static_cast<std::size_t>(x);
}

// The land border edges of each pixel, by kind: river, ridge, coast, hostile land, other.
std::vector<Counts> borderPixels(const settlement::ExpansionTerrain& t,
                                 const settlement::Kingdoms& k, const settlement::BorderRule& rule,
                                 const Image& image, int scale) {
    std::vector<Counts> counts(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), Counts{});
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
            const std::size_t kind = borderKind(settlement::edgeKinds(t, a, b, east, rule));
            std::uint16_t& c = counts[pixelOf(g, image, scale, cell)][kind];
            c = static_cast<std::uint16_t>(std::min(c + 1, 0xFFFF));
        }
    });
    return counts;
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
                planet_png::put(image, x, y,
                                settlement_png::stateColor(s, kingdoms.states[s].rank));
            }
        }
    }
    const auto counts = borderPixels(terrain, kingdoms, rule, image, scale);
    for (std::size_t p = 0; p < counts.size(); ++p) {
        const auto& c = counts[p];
        const auto most = std::max_element(c.begin(), c.end());
        if (*most > 0) {
            const auto kind = static_cast<std::size_t>(most - c.begin());
            planet_png::put(image, static_cast<int>(p % static_cast<std::size_t>(image.width)),
                            static_cast<int>(p / static_cast<std::size_t>(image.width)),
                            settlement_png::kBorderColors[kind]);
        }
    }
    for (std::size_t s = 0; s < kingdoms.states.size(); ++s) {
        const std::size_t p = pixelOf(grid, image, scale, kingdoms.capitals.capitals[s].cell);
        const bool major = kingdoms.states[s].rank == settlement::StateRank::Major;
        settlement_png::capitalSquare(image,
                                      static_cast<int>(p % static_cast<std::size_t>(image.width)),
                                      static_cast<int>(p / static_cast<std::size_t>(image.width)),
                                      major ? 2 : 0);
    }
    return image;
}

} // namespace terrain
