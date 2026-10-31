#pragma once

// Internal to the settlement images: the colors of the states and of the kinds of borders, shared
// by the whole map and its zooms.

#include "planet_png_draw.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/settlement/borders.hpp"
#include "terrain/generation/settlement/world_preset.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace terrain::settlement_png {

using planet_png::Rgb;

// A color of a hue in [0, 1) at a saturation and a value (HSV).
inline Rgb hsv(double hue, double saturation, double value) {
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

// A hue of its own for an index.
inline double hueOf(std::uint64_t index, std::uint64_t salt) {
    return static_cast<double>(noise::deriveSeed(index, salt) >> 11) * 0x1.0p-53;
}

// The color of a state: its own hue, saturated for a major kingdom, paler for a minor state.
inline Rgb stateColor(std::size_t state, settlement::StateRank rank) {
    const double hue = hueOf(state, 0x6B696E67);
    return rank == settlement::StateRank::Major ? hsv(hue, 0.75, 0.85) : hsv(hue, 0.35, 0.97);
}

// The kinds of a border edge in the order they are drawn: river, ridge, coast, hostile land, none.
inline constexpr std::size_t kBorderKinds = 5;
inline constexpr std::array<Rgb, kBorderKinds> kBorderColors = {{{40.0f, 110.0f, 255.0f},
                                                                 {255.0f, 255.0f, 255.0f},
                                                                 {0.0f, 230.0f, 230.0f},
                                                                 {255.0f, 150.0f, 0.0f},
                                                                 {110.0f, 0.0f, 0.0f}}};

// The first kind of the kinds of an edge (edgeKinds), as an index of kBorderColors.
inline std::size_t borderKind(std::uint8_t kinds) {
    if ((kinds & settlement::kAlongRiver) != 0) {
        return 0;
    }
    if ((kinds & settlement::kAlongRidge) != 0) {
        return 1;
    }
    if ((kinds & settlement::kAlongCoast) != 0) {
        return 2;
    }
    return (kinds & settlement::kAlongHostile) != 0 ? 3 : 4;
}

// A white square with a dark outline, wrapped.
inline void capitalSquare(Image& image, int cx, int cy, int half) {
    for (int dy = -half - 1; dy <= half + 1; ++dy) {
        for (int dx = -half - 1; dx <= half + 1; ++dx) {
            const bool edge = std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) == half + 1;
            const int x = ((cx + dx) % image.width + image.width) % image.width;
            const int y = ((cy + dy) % image.height + image.height) % image.height;
            planet_png::put(image, x, y,
                            edge ? Rgb{20.0f, 20.0f, 20.0f} : Rgb{255.0f, 255.0f, 255.0f});
        }
    }
}

} // namespace terrain::settlement_png
