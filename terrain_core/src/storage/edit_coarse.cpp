#include "terrain/storage/edit_coarse.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <limits>
#include <optional>

namespace terrain {

namespace {

constexpr std::size_t kAdded = 0;
constexpr std::size_t kRemoved = 1;

// The weight of a tap of a cell along an axis, at offset from its center: 1 on its sides, 2
// inside (its cover counts the blocks of its sides half).
int tapWeight(int offset, int s) {
    return offset == -s / 2 || offset == s / 2 ? 1 : 2;
}

// The weights of the taps [a, b] of a cell of center c (a and b within s / 2 of it).
int tapWeights(int a, int b, int c, int s) {
    if (a > b) {
        return 0;
    }
    return 2 * (b - a + 1) - (a == c - s / 2 ? 1 : 0) - (b == c + s / 2 ? 1 : 0);
}

// Adds the blocks [lo, hi] (each axis) of the half-open cell to the box and the count of a part.
void addToBox(std::array<int, 3> lo, std::array<int, 3> hi, int blocks, CoarsePart& part) {
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

float coverOf(const CoarsePart& part, int s) {
    return static_cast<float>(part.weight) / static_cast<float>(8 * s * s * s);
}

float weigh(float density, float target, float weight) {
    return weight >= 1.0f ? target : density + weight * (target - density);
}

float edited(float density, bool removing, float sdf) {
    return removing ? std::max(density, sdf) : std::min(density, -sdf);
}

// What covers half a cell or more: the density s (cover - 1/2), from the cover of the cell of the
// point (own) and the largest cover of the 27 cells around it.
float coverTerm(float density, bool removing, float own, float largest, int s) {
    const float ramp = std::clamp((largest - 0.25f) * 4.0f, 0.0f, 1.0f);
    if (ramp <= 0.0f) {
        return density;
    }
    return weigh(density, edited(density, removing, static_cast<float>(s) * (own - 0.5f)), ramp);
}

// The 16 bits of the blocks of an edit in the row (ly, lz) of the masks of a section.
std::uint32_t maskRow(const std::array<SectionMask, 2>& masks, std::size_t edit, int ly, int lz) {
    const int first = sectionIndex(0, ly, lz);
    const std::uint64_t word = masks[edit][static_cast<std::size_t>(first / 64)];
    return static_cast<std::uint32_t>((word >> (first % 64)) & 0xFFFFU);
}

std::unique_ptr<std::array<SectionMask, 2>> masksOf(const BlockSection& section) {
    auto masks = std::make_unique<std::array<SectionMask, 2>>();
    section.toMasks((*masks)[kRemoved], (*masks)[kAdded]);
    return masks;
}

} // namespace

// The sections of the blocks around the samples, read once from the window, with the masks of
// those not filled with one edit as asked.
class CoarseEdits::Sections {
public:
    Sections(const EditWindow& window, std::array<int, 3> low, std::array<int, 3> high) {
        for (std::size_t a = 0; a < 3; ++a) {
            first_[a] = low[a] >> kSectionShift;
            count_[a] = (high[a] >> kSectionShift) - first_[a] + 1;
        }
        sections_.resize(static_cast<std::size_t>(count_[0] * count_[1] * count_[2]));
        masks_.resize(sections_.size());
        for (int k = 0; k < count_[2]; ++k) {
            for (int j = 0; j < count_[1]; ++j) {
                for (int i = 0; i < count_[0]; ++i) {
                    const BlockSection* section =
                        window.section(first_[0] + i, first_[1] + j, first_[2] + k);
                    sections_[slot(i, j, k)] = section;
                    if (section == nullptr) {
                        continue;
                    }
                    const std::array<int, 3> at{first_[0] + i, first_[1] + j, first_[2] + k};
                    for (std::size_t a = 0; a < 3; ++a) {
                        lowest_[a] = any_ ? std::min(lowest_[a], at[a]) : at[a];
                        highest_[a] = any_ ? std::max(highest_[a], at[a]) : at[a];
                    }
                    any_ = true;
                }
            }
        }
    }
    [[nodiscard]] bool any() const { return any_; }
    // The lowest and highest blocks of the sections holding edits, along an axis.
    [[nodiscard]] std::pair<int, int> blocks(std::size_t axis) const {
        return {lowest_[axis] * kSectionSide, highest_[axis] * kSectionSide + kSectionSide - 1};
    }
    [[nodiscard]] const BlockSection* at(int sx, int sy, int sz) const {
        return sections_[slot(sx - first_[0], sy - first_[1], sz - first_[2])];
    }
    std::uint32_t row(int sx, int sy, int sz, std::size_t edit, int ly, int lz) {
        const std::size_t s = slot(sx - first_[0], sy - first_[1], sz - first_[2]);
        if (!masks_[s]) {
            masks_[s] = masksOf(*sections_[s]);
        }
        return maskRow(*masks_[s], edit, ly, lz);
    }

private:
    [[nodiscard]] std::size_t slot(int i, int j, int k) const {
        return static_cast<std::size_t>((k * count_[1] + j) * count_[0] + i);
    }

    std::array<int, 3> first_{};
    std::array<int, 3> count_{};
    std::vector<const BlockSection*> sections_;
    std::vector<std::unique_ptr<std::array<SectionMask, 2>>> masks_;
    std::array<int, 3> lowest_{};
    std::array<int, 3> highest_{};
    bool any_ = false;
};

namespace {

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a - 1) / b) - 1;
}

} // namespace

CoarseEdits::CoarseEdits(const EditWindow& window, int x0, int y0, int z0, int spacing,
                         std::array<int, 3> counts, float relay)
    : x0_(x0), y0_(y0), z0_(z0), spacing_(spacing), counts_(counts), relay_(relay),
      beyond_(relay > 0.0f ? 2 : 1) {
    assert(spacing >= 2 && spacing % 2 == 0 && spacing <= 16);
    const int s = spacing;
    const std::array<int, 3> origin{x0, y0, z0};
    std::array<int, 3> low{};
    std::array<int, 3> high{};
    for (std::size_t a = 0; a < 3; ++a) {
        low[a] = origin[a] - beyond_ * s - s / 2;
        high[a] = origin[a] + (counts[a] - 1 + beyond_) * s + s / 2;
    }
    if (window.empty() || !window.meets({low[0], low[1], low[2]}, {high[0], high[1], high[2]})) {
        return;
    }
    Sections sections(window, low, high);
    if (!sections.any()) {
        return;
    }
    // The cells whose taps meet the sections holding edits, and the cells read around them (the
    // 27 cells of a point near them, and their neighbors for the relay): the others hold nothing.
    std::array<int, 3> content{};
    for (std::size_t a = 0; a < 3; ++a) {
        const auto [first, last] = sections.blocks(a);
        const int lo = std::max(-beyond_, floorDiv(first - s / 2 - origin[a] + s - 1, s));
        const int hi = std::min(counts[a] + beyond_ - 1, floorDiv(last + s / 2 - origin[a], s));
        regionLow_[a] = std::max(-beyond_, lo - 2);
        regionCount_[a] = std::min(counts[a] + beyond_ - 1, hi + 2) - regionLow_[a] + 1;
        content[a] = lo;
        contentHigh_[a] = hi;
    }
    contentLow_ = content;
    cells_.resize(static_cast<std::size_t>(regionCount_[0] * regionCount_[1] * regionCount_[2]));
    near_.assign(cells_.size(), 0);
    for (int k = content[2]; k <= contentHigh_[2]; ++k) {
        for (int j = content[1]; j <= contentHigh_[1]; ++j) {
            for (int i = content[0]; i <= contentHigh_[0]; ++i) {
                const CoarseCell cell =
                    measureCell(sections, {x0 + i * s, y0 + j * s, z0 + k * s}, s);
                cells_[index(i, j, k)] = cell;
                if (cell[kAdded].weight + cell[kRemoved].weight != 0) {
                    markNear(i, j, k);
                }
            }
        }
    }
    snapBoxes();
}

// The points around a cell holding an edited block read it (their 27 cells).
void CoarseEdits::markNear(int i, int j, int k) {
    for (int dk = k - 1; dk <= k + 1; ++dk) {
        for (int dj = j - 1; dj <= j + 1; ++dj) {
            for (int di = i - 1; di <= i + 1; ++di) {
                if (inRegion(di, dj, dk)) {
                    near_[index(di, dj, dk)] = 1;
                }
            }
        }
    }
}

bool CoarseEdits::inRegion(int i, int j, int k) const {
    return i >= regionLow_[0] && i < regionLow_[0] + regionCount_[0] && j >= regionLow_[1] &&
           j < regionLow_[1] + regionCount_[1] && k >= regionLow_[2] &&
           k < regionLow_[2] + regionCount_[2];
}

std::size_t CoarseEdits::index(int i, int j, int k) const {
    return static_cast<std::size_t>(((k - regionLow_[2]) * regionCount_[1] + j - regionLow_[1]) *
                                        regionCount_[0] +
                                    i - regionLow_[0]);
}

// A box thinner than its cell along x or z, with no block of its edit in the cells beside it
// along that axis, moves onto the column of its cell (its center on the center of the cell).
void CoarseEdits::snapBoxes() {
    if (relay_ <= 0.0f) {
        return; // no box is read
    }
    const int s = spacing_;
    const std::array<int, 3> origin{x0_, y0_, z0_};
    // The cells holding blocks whose neighbors are known (as far as the samples need).
    for (int k = std::max(contentLow_[2], 1 - beyond_);
         k <= std::min(contentHigh_[2], counts_[2] + beyond_ - 2); ++k) {
        for (int j = std::max(contentLow_[1], 1 - beyond_);
             j <= std::min(contentHigh_[1], counts_[1] + beyond_ - 2); ++j) {
            for (int i = std::max(contentLow_[0], 1 - beyond_);
                 i <= std::min(contentHigh_[0], counts_[0] + beyond_ - 2); ++i) {
                const std::array<int, 3> at{i, j, k};
                for (std::size_t e = 0; e < 2; ++e) {
                    CoarsePart& part = cells_[index(i, j, k)][e];
                    if (part.count == 0) {
                        continue;
                    }
                    for (const std::size_t a : {std::size_t{0}, std::size_t{2}}) {
                        std::array<int, 3> before = at;
                        std::array<int, 3> after = at;
                        --before[a];
                        ++after[a];
                        const bool alone = cell(before[0], before[1], before[2])[e].count == 0 &&
                                           cell(after[0], after[1], after[2])[e].count == 0;
                        if (part.high[a] - part.low[a] < static_cast<float>(s) && alone) {
                            const auto center = static_cast<float>(origin[a] + at[a] * s);
                            const float shift = center - 0.5f * (part.low[a] + part.high[a]);
                            part.low[a] += shift;
                            part.high[a] += shift;
                        }
                    }
                }
            }
        }
    }
}

float CoarseEdits::applyEdit(float density, std::size_t edit, int i, int j, int k) const {
    const bool removing = edit == kRemoved;
    const std::array<double, 3> p{static_cast<double>(x0_ + i * spacing_),
                                  static_cast<double>(y0_ + j * spacing_),
                                  static_cast<double>(z0_ + k * spacing_)};
    float largest = 0.0f;
    bool boxed = false;
    double inside = -std::numeric_limits<double>::max(); // into the nearest box, meters
    for (int dk = -1; dk <= 1; ++dk) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const CoarsePart& part = cell(i + di, j + dj, k + dk)[edit];
                const float c = coverOf(part, spacing_);
                largest = std::max(largest, c);
                if (relay_ <= 0.0f || part.count == 0 || c >= 0.5f) {
                    continue;
                }
                std::array<double, 3> q{};
                for (std::size_t a = 0; a < 3; ++a) {
                    q[a] = std::max(static_cast<double>(part.low[a]) - p[a],
                                    p[a] - static_cast<double>(part.high[a])); // > 0 outside
                }
                const double outside = dmath::hypot(std::max(q[0], 0.0), std::max(q[1], 0.0),
                                                    std::max(q[2], 0.0));
                const double within = std::min(std::max({q[0], q[1], q[2]}), 0.0);
                inside = std::max(inside, -(outside + within));
                boxed = true;
            }
        }
    }
    float result =
        coverTerm(density, removing, coverOf(cell(i, j, k)[edit], spacing_), largest, spacing_);
    if (boxed) {
        const float relayed =
            weigh(density, edited(density, removing, static_cast<float>(inside)), relay_);
        result = removing ? std::max(result, relayed) : std::min(result, relayed);
    }
    return result;
}

float CoarseEdits::density(float generated, int i, int j, int k) const {
    if (cells_.empty() || !inRegion(i, j, k) || near_[index(i, j, k)] == 0) {
        return generated; // no edited block in the cells around the point
    }
    return applyEdit(applyEdit(generated, kAdded, i, j, k), kRemoved, i, j, k);
}

// The sections of a window as asked, with their masks.
class CoarseCellCache::Sections {
public:
    explicit Sections(const EditWindow& window) : window_(window) {}

    [[nodiscard]] const BlockSection* at(int sx, int sy, int sz) {
        return entry(sx, sy, sz).section;
    }
    std::uint32_t row(int sx, int sy, int sz, std::size_t edit, int ly, int lz) {
        Entry& e = entry(sx, sy, sz);
        if (!e.masks) {
            e.masks = masksOf(*e.section);
        }
        return maskRow(*e.masks, edit, ly, lz);
    }

private:
    struct Entry {
        const BlockSection* section = nullptr;
        std::unique_ptr<std::array<SectionMask, 2>> masks;
    };
    Entry& entry(int sx, int sy, int sz) {
        const std::array<int, 3> key{sx, sy, sz};
        if (last_ != nullptr && key == lastKey_) {
            return *last_; // the cells of a column read the same sections in turn
        }
        const auto [it, added] = entries_.try_emplace(key);
        if (added) {
            it->second.section = window_.section(sx, sy, sz);
        }
        lastKey_ = key;
        last_ = &it->second;
        return it->second;
    }

    const EditWindow& window_;
    std::unordered_map<std::array<int, 3>, Entry, KeyHash> entries_; // nodes: stable addresses
    std::array<int, 3> lastKey_{};
    Entry* last_ = nullptr;
};

std::size_t CoarseCellCache::KeyHash::operator()(const std::array<int, 3>& key) const {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const int v : key) {
        h = (h ^ static_cast<std::uint32_t>(v)) * 0x100000001B3ULL;
    }
    return static_cast<std::size_t>(h);
}

CoarseCellCache::CoarseCellCache(const EditWindow& window, int spacing)
    : spacing_(spacing), sections_(std::make_unique<Sections>(window)) {}

CoarseCellCache::~CoarseCellCache() = default;

const CoarseCell& CoarseCellCache::cellAt(const std::array<int, 3>& center) {
    const auto [it, added] = cells_.try_emplace(center);
    if (added) {
        it->second = measureCell(*sections_, center, spacing_);
    }
    return it->second;
}

CoarseCellCache::Level CoarseCellCache::level(int x, int y, int z) {
    Level level;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const CoarseCell& cell = cellAt({x + dx * spacing_, y, z + dz * spacing_});
            for (std::size_t e = 0; e < 2; ++e) {
                level.largest[e] = std::max(level.largest[e], coverOf(cell[e], spacing_));
            }
        }
    }
    const CoarseCell& own = cellAt({x, y, z});
    level.own = {coverOf(own[kAdded], spacing_), coverOf(own[kRemoved], spacing_)};
    return level;
}

float CoarseCellCache::density(float generated, const Level& below, const Level& here,
                               const Level& above) const {
    float density = generated;
    for (const std::size_t edit : {kAdded, kRemoved}) {
        const float largest =
            std::max({below.largest[edit], here.largest[edit], above.largest[edit]});
        density = coverTerm(density, edit == kRemoved, here.own[edit], largest, spacing_);
    }
    return density;
}

float CoarseCellCache::density(float generated, int x, int y, int z) {
    return density(generated, level(x, y - spacing_, z), level(x, y, z),
                   level(x, y + spacing_, z));
}

} // namespace terrain
