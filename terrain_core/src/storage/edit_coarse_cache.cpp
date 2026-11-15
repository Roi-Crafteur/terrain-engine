#include "terrain/storage/edit_coarse.hpp"

#include "edit_coarse_cells.hpp"

#include <unordered_map>

namespace terrain {

using coarse_cells::coverOf;
using coarse_cells::coverTerm;
using coarse_cells::kAdded;
using coarse_cells::kRemoved;
using coarse_cells::maskRow;
using coarse_cells::masksOf;
using coarse_cells::measureCell;

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
