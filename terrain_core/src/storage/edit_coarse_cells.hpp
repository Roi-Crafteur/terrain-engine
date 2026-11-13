#pragma once

// The cells of the coarse lattices (agent 41), internal to edit_coarse.cpp and
// edit_coarse_cache.cpp: the cover and the box of each edit in the cell of a point, from the
// sections of the blocks, and the density of what covers half a cell or more.

#include "terrain/storage/edit_coarse.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>

namespace terrain::coarse_cells {

inline constexpr std::size_t kAdded = 0;
inline constexpr std::size_t kRemoved = 1;

// The weight of a tap of a cell along an axis, at offset from its center: 1 on its sides, 2
// inside (its cover counts the blocks of its sides half).
inline int tapWeight(int offset, int s) {
    return offset == -s / 2 || offset == s / 2 ? 1 : 2;
}

// The weights of the taps [a, b] of a cell of center c (a and b within s / 2 of it).
inline int tapWeights(int a, int b, int c, int s) {
    if (a > b) {
        return 0;
    }
    return 2 * (b - a + 1) - (a == c - s / 2 ? 1 : 0) - (b == c + s / 2 ? 1 : 0);
}

// Adds the blocks [lo, hi] (each axis) of the half-open cell to the box and the count of a part.
inline void addToBox(std::array<int, 3> lo, std::array<int, 3> hi, int blocks, CoarsePart& part) {
    for (std::size_t a = 0; a < 3; ++a) {
        const float l = static_cast<float>(lo[a]) - 0.5f;
        const float h = static_cast<float>(hi[a]) + 0.5f;
        part.low[a] = part.count == 0 ? l : std::min(part.low[a], l);
        part.high[a] = part.count == 0 ? h : std::max(part.high[a], h);
    }
    part.count = static_cast<std::uint16_t>(part.count + blocks);
}

// The rows of a section (not filled with one edit) in the taps [lo, hi] of the cell of center c,
// added to the parts of the cell. Source: at(sx, sy, sz), row(sx, sy, sz, edit, ly, lz).
template <class Source>
void addRows(Source& sections, std::array<int, 3> section, std::array<int, 3> lo,
             std::array<int, 3> hi, std::array<int, 3> c, int s, CoarseCell& cell) {
    const int h = s / 2;
    const std::array<int, 3> base{section[0] * kSectionSide, section[1] * kSectionSide,
                                  section[2] * kSectionSide};
    const int top = std::min(hi[0], c[0] + h - 1); // the high end of the half-open cell
    const std::uint32_t mask = ((1U << (hi[0] - lo[0] + 1)) - 1U) << (lo[0] - base[0]);
    for (std::size_t e = 0; e < 2; ++e) {
        for (int z = lo[2]; z <= hi[2]; ++z) {
            for (int y = lo[1]; y <= hi[1]; ++y) {
                const std::uint32_t row =
                    sections.row(section[0], section[1], section[2], e, y - base[1], z - base[2]);
                const std::uint32_t bits = (row & mask) >> (lo[0] - base[0]);
                if (bits == 0) {
                    continue;
                }
                // Along x: 2 a block, 1 on a side of the cell.
                int weight = 2 * std::popcount(bits);
                weight -= lo[0] == c[0] - h ? static_cast<int>(bits & 1U) : 0;
                weight -= hi[0] == c[0] + h ? static_cast<int>((bits >> (hi[0] - lo[0])) & 1U) : 0;
                cell[e].weight += static_cast<std::uint32_t>(weight * tapWeight(y - c[1], s) *
                                                             tapWeight(z - c[2], s));
                const std::uint32_t half = top >= lo[0] ? bits & ((2U << (top - lo[0])) - 1U) : 0U;
                if (half == 0 || y >= c[1] + h || z >= c[2] + h) {
                    continue;
                }
                addToBox({lo[0] + std::countr_zero(half), y, z},
                         {lo[0] + 31 - std::countl_zero(half), y, z}, std::popcount(half),
                         cell[e]);
            }
        }
    }
}

// The cover and the box of each edit in the cell of center c: section by section, a section
// filled with one edit at once, the others row by row of their masks.
template <class Source>
CoarseCell measureCell(Source& sections, std::array<int, 3> c, int s) {
    CoarseCell cell{};
    const int h = s / 2;
    for (int sz = (c[2] - h) >> kSectionShift; sz <= (c[2] + h) >> kSectionShift; ++sz) {
        for (int sy = (c[1] - h) >> kSectionShift; sy <= (c[1] + h) >> kSectionShift; ++sy) {
            for (int sx = (c[0] - h) >> kSectionShift; sx <= (c[0] + h) >> kSectionShift; ++sx) {
                const BlockSection* section = sections.at(sx, sy, sz);
                if (section == nullptr) {
                    continue;
                }
                const std::array<int, 3> at{sx, sy, sz};
                std::array<int, 3> lo{};
                std::array<int, 3> hi{};
                std::array<int, 3> top{};
                for (std::size_t a = 0; a < 3; ++a) {
                    lo[a] = std::max(c[a] - h, at[a] * kSectionSide);
                    hi[a] = std::min(c[a] + h, at[a] * kSectionSide + kSectionSide - 1);
                    top[a] = std::min(hi[a], c[a] + h - 1);
                }
                const std::optional<BlockEdit> uniform = section->uniform();
                if (!uniform) {
                    addRows(sections, at, lo, hi, c, s, cell);
                    continue;
                }
                CoarsePart& part = cell[*uniform == BlockEdit::Added ? kAdded : kRemoved];
                part.weight += static_cast<std::uint32_t>(tapWeights(lo[0], hi[0], c[0], s) *
                                                          tapWeights(lo[1], hi[1], c[1], s) *
                                                          tapWeights(lo[2], hi[2], c[2], s));
                if (top[0] >= lo[0] && top[1] >= lo[1] && top[2] >= lo[2]) {
                    addToBox(lo, top,
                             (top[0] - lo[0] + 1) * (top[1] - lo[1] + 1) * (top[2] - lo[2] + 1),
                             part);
                }
            }
        }
    }
    return cell;
}

inline float coverOf(const CoarsePart& part, int s) {
    return static_cast<float>(part.weight) / static_cast<float>(8 * s * s * s);
}

inline float weigh(float density, float target, float weight) {
    return weight >= 1.0f ? target : density + weight * (target - density);
}

inline float edited(float density, bool removing, float sdf) {
    return removing ? std::max(density, sdf) : std::min(density, -sdf);
}

// What covers half a cell or more: the density s (cover - 1/2), from the cover of the cell of the
// point (own) and the largest cover of the 27 cells around it.
inline float coverTerm(float density, bool removing, float own, float largest, int s) {
    const float ramp = std::clamp((largest - 0.25f) * 4.0f, 0.0f, 1.0f);
    if (ramp <= 0.0f) {
        return density;
    }
    return weigh(density, edited(density, removing, static_cast<float>(s) * (own - 0.5f)), ramp);
}

// The 16 bits of the blocks of an edit in the row (ly, lz) of the masks of a section.
inline std::uint32_t maskRow(const std::array<SectionMask, 2>& masks, std::size_t edit, int ly,
                             int lz) {
    const int first = sectionIndex(0, ly, lz);
    const std::uint64_t word = masks[edit][static_cast<std::size_t>(first / 64)];
    return static_cast<std::uint32_t>((word >> (first % 64)) & 0xFFFFU);
}

inline std::unique_ptr<std::array<SectionMask, 2>> masksOf(const BlockSection& section) {
    auto masks = std::make_unique<std::array<SectionMask, 2>>();
    section.toMasks((*masks)[kRemoved], (*masks)[kAdded]);
    return masks;
}


} // namespace terrain::coarse_cells
