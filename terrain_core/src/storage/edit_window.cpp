#include "terrain/storage/edit_window.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

static_assert(kBlockReach == 1, "the edits read the 3 x 3 x 3 blocks around a lattice point");

namespace {

constexpr std::uint8_t kRemovedState = 1;
constexpr std::uint8_t kAddedState = 2;

// Index of the block at the offset (dx, dy, dz) of a lattice point, each in [-1, 1].
constexpr std::size_t offsetIndex(int dx, int dy, int dz) {
    return static_cast<std::size_t>(((dz + 1) * 3 + (dy + 1)) * 3 + (dx + 1));
}

// Signed distances from a lattice point to the cubes of the blocks around it (blockDistance).
const std::array<float, 27>& cubeDistances() {
    static const std::array<float, 27> distances = [] {
        std::array<float, 27> d{};
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    d[offsetIndex(dx, dy, dz)] = blockDistance({0, 0, 0}, dx, dy, dz);
                }
            }
        }
        return d;
    }();
    return distances;
}

// The nearest cubes of the added and of the removed blocks around a lattice point. Every added
// block first (min), then every removed one (max), as editDensity block by block: min and max
// are exact, so their order never changes a bit.
class NearestCubes {
public:
    void add(std::uint8_t state, std::size_t offset) {
        if (state == 0) {
            return;
        }
        const float cube = cubeDistances()[offset];
        if (state == kAddedState) {
            added_ = anyAdded_ ? std::min(added_, cube) : cube;
            anyAdded_ = true;
        } else {
            removed_ = anyRemoved_ ? std::min(removed_, cube) : cube;
            anyRemoved_ = true;
        }
    }
    [[nodiscard]] float apply(float density) const {
        if (anyAdded_) {
            density = std::min(density, added_);
        }
        if (anyRemoved_) {
            density = std::max(density, -removed_);
        }
        return density;
    }

private:
    float added_ = 0.0f;
    float removed_ = 0.0f;
    bool anyAdded_ = false;
    bool anyRemoved_ = false;
};

std::uint8_t stateOf(std::optional<BlockEdit> edit) {
    if (!edit) {
        return 0;
    }
    return *edit == BlockEdit::Removed ? kRemovedState : kAddedState;
}

} // namespace

EditWindow::EditWindow(const ModificationStore& store, int minX, int maxX, int minZ, int maxZ,
                       const WorldSize& world)
    : minX_(minX), maxX_(maxX), minZ_(minZ), maxZ_(maxZ), cx0_(minX >> kSectionShift),
      cz0_(minZ >> kSectionShift), nx_((maxX >> kSectionShift) - cx0_ + 1),
      nz_((maxZ >> kSectionShift) - cz0_ + 1) {
    columns_.resize(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(nz_));
    for (int k = 0; k < nz_; ++k) {
        for (int i = 0; i < nx_; ++i) {
            const ChunkModifications* chunk = store.chunk(wrapChunk({cx0_ + i, cz0_ + k}, world));
            if (chunk == nullptr || chunk->sections.empty()) {
                continue;
            }
            Column& column = columns_[static_cast<std::size_t>(k * nx_ + i)];
            column.ys.reserve(chunk->sections.size());
            column.sections.reserve(chunk->sections.size());
            for (const auto& [y, section] : chunk->sections) {
                column.ys.push_back(y);
                column.sections.push_back(section);
            }
            edited_ = true;
        }
    }
}

const EditWindow::Column* EditWindow::column(std::int32_t cx, std::int32_t cz) const {
    const std::int64_t i = std::int64_t{cx} - cx0_;
    const std::int64_t k = std::int64_t{cz} - cz0_;
    if (i < 0 || k < 0 || i >= nx_ || k >= nz_) {
        return nullptr;
    }
    return &columns_[static_cast<std::size_t>(k * nx_ + i)];
}

const BlockSection* EditWindow::section(std::int32_t cx, std::int32_t sy, std::int32_t cz) const {
    const Column* found = column(cx, cz);
    if (found == nullptr) {
        return nullptr;
    }
    const auto it = std::lower_bound(found->ys.begin(), found->ys.end(), sy);
    if (it == found->ys.end() || *it != sy) {
        return nullptr;
    }
    return &found->sections[static_cast<std::size_t>(it - found->ys.begin())];
}

std::optional<BlockEdit> EditWindow::blockAt(int x, int y, int z) const {
    if (!inArea(x, z)) {
        return std::nullopt;
    }
    const BlockSection* found =
        section(x >> kSectionShift, sectionOf(y), z >> kSectionShift);
    return found == nullptr ? std::nullopt
                            : found->at(sectionIndex(localOf(x), localOf(y), localOf(z)));
}

float EditWindow::density(float generated, int x, int y, int z) const {
    NearestCubes nearest;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                nearest.add(stateOf(blockAt(x + dx, y + dy, z + dz)), offsetIndex(dx, dy, dz));
            }
        }
    }
    return nearest.apply(generated);
}

std::vector<std::pair<int, int>> EditWindow::editedSpans() const {
    std::vector<std::pair<int, int>> spans;
    for (int k = 0; k < nz_; ++k) {
        for (int i = 0; i < nx_; ++i) {
            const Column& column = columns_[static_cast<std::size_t>(k * nx_ + i)];
            const int x0 = (cx0_ + i) * kSectionSide;
            const int z0 = (cz0_ + k) * kSectionSide;
            const int lx0 = std::max(minX_, x0) - x0;
            const int lx1 = std::min(maxX_, x0 + kSectionSide - 1) - x0;
            const int lz0 = std::max(minZ_, z0) - z0;
            const int lz1 = std::min(maxZ_, z0 + kSectionSide - 1) - z0;
            for (std::size_t s = 0; s < column.ys.size(); ++s) {
                if (const auto layers = column.sections[s].editedLayers(lx0, lx1, lz0, lz1)) {
                    const int base = column.ys[s] * kSectionSide;
                    spans.emplace_back(base + layers->first, base + layers->second);
                }
            }
        }
    }
    return spans;
}

bool EditWindow::filled(BlockCoord min, BlockCoord max) const {
    if (!inArea(min.x, min.z) || !inArea(max.x, max.z)) {
        return false;
    }
    std::optional<BlockEdit> edit;
    for (std::int32_t cz = min.z >> kSectionShift; cz <= max.z >> kSectionShift; ++cz) {
        for (std::int32_t cx = min.x >> kSectionShift; cx <= max.x >> kSectionShift; ++cx) {
            for (std::int32_t sy = sectionOf(min.y); sy <= sectionOf(max.y); ++sy) {
                const BlockSection* found = section(cx, sy, cz);
                const std::optional<BlockEdit> uniform =
                    found == nullptr ? std::nullopt : found->uniform();
                if (!uniform || (edit && uniform != edit)) {
                    return false;
                }
                edit = uniform;
            }
        }
    }
    return true;
}

bool EditWindow::meets(BlockCoord min, BlockCoord max) const {
    const std::int32_t sy0 = sectionOf(min.y);
    const std::int32_t sy1 = sectionOf(max.y);
    for (std::int32_t cz = min.z >> kSectionShift; cz <= max.z >> kSectionShift; ++cz) {
        for (std::int32_t cx = min.x >> kSectionShift; cx <= max.x >> kSectionShift; ++cx) {
            const Column* found = column(cx, cz);
            if (found == nullptr) {
                continue;
            }
            const auto it = std::lower_bound(found->ys.begin(), found->ys.end(), sy0);
            if (it != found->ys.end() && *it <= sy1) {
                return true;
            }
        }
    }
    return false;
}

std::size_t EditWindow::heapBytes() const {
    std::size_t bytes = columns_.capacity() * sizeof(Column);
    for (const Column& column : columns_) {
        bytes += column.ys.capacity() * sizeof(std::int32_t) +
                 column.sections.capacity() * sizeof(BlockSection);
        for (const BlockSection& section : column.sections) {
            bytes += section.heapBytes();
        }
    }
    return bytes;
}

int EditLattice::Axis::coordinate(int index) const {
    return spaced() ? origin + (index / 3) * spacing + index % 3 - 1 : origin - 1 + index;
}

int EditLattice::Axis::indexOf(int c) const {
    const int t = c - origin + 1;
    if (t < 0) {
        return -1;
    }
    if (!spaced()) {
        return t < size() ? t : -1;
    }
    const int i = t / spacing;
    const int r = t % spacing;
    return r <= 2 && i < count ? 3 * i + r : -1;
}

std::pair<int, int> EditLattice::Axis::range(int low, int high) const {
    int first = 0;
    while (first < size() && coordinate(first) < low) {
        ++first;
    }
    int last = size() - 1;
    while (last >= 0 && coordinate(last) > high) {
        --last;
    }
    return {first, last};
}

EditLattice::EditLattice(const EditWindow& window, int x0, int y0, int z0, int spacing,
                         int count)
    : x_{x0, spacing, count}, y_{y0, spacing, count}, z_{z0, spacing, count} {
    const BlockCoord min{x_.coordinate(0), y_.coordinate(0), z_.coordinate(0)};
    const BlockCoord max{x_.coordinate(x_.size() - 1), y_.coordinate(y_.size() - 1),
                         z_.coordinate(z_.size() - 1)};
    if (window.empty() || !window.meets(min, max)) {
        return;
    }
    states_.assign(static_cast<std::size_t>(x_.size()) * static_cast<std::size_t>(y_.size()) *
                       static_cast<std::size_t>(z_.size()),
                   0);
    for (std::int32_t cz = min.z >> kSectionShift; cz <= max.z >> kSectionShift; ++cz) {
        for (std::int32_t cx = min.x >> kSectionShift; cx <= max.x >> kSectionShift; ++cx) {
            for (std::int32_t sy = sectionOf(min.y); sy <= sectionOf(max.y); ++sy) {
                fill(window, cx, sy, cz);
            }
        }
    }
    if (std::all_of(states_.begin(), states_.end(), [](std::uint8_t s) { return s == 0; })) {
        std::vector<std::uint8_t>().swap(states_);
    }
}

void EditLattice::fill(const EditWindow& window, std::int32_t cx, std::int32_t sy,
                       std::int32_t cz) {
    const BlockSection* section = window.section(cx, sy, cz);
    if (section == nullptr) {
        return;
    }
    const int bx = cx * kSectionSide;
    const int by = sy * kSectionSide;
    const int bz = cz * kSectionSide;
    if (const std::optional<BlockEdit> uniform = section->uniform()) {
        const std::uint8_t state = stateOf(uniform);
        const auto [ix0, ix1] = x_.range(std::max(bx, window.minX_),
                                         std::min(bx + kSectionSide - 1, window.maxX_));
        const auto [iy0, iy1] = y_.range(by, by + kSectionSide - 1);
        const auto [iz0, iz1] = z_.range(std::max(bz, window.minZ_),
                                         std::min(bz + kSectionSide - 1, window.maxZ_));
        for (int iz = iz0; iz <= iz1; ++iz) {
            for (int iy = iy0; iy <= iy1; ++iy) {
                for (int ix = ix0; ix <= ix1; ++ix) {
                    states_[at(ix, iy, iz)] = state;
                }
            }
        }
        return;
    }
    section->forEach([&](int index, BlockEdit edit) {
        const int x = bx + index % kSectionSide;
        const int y = by + index / (kSectionSide * kSectionSide);
        const int z = bz + (index / kSectionSide) % kSectionSide;
        const int ix = x_.indexOf(x);
        const int iy = y_.indexOf(y);
        const int iz = z_.indexOf(z);
        if (ix >= 0 && iy >= 0 && iz >= 0 && window.inArea(x, z)) {
            states_[at(ix, iy, iz)] = stateOf(edit);
        }
    });
}

float EditLattice::density(float generated, int i, int j, int k) const {
    if (states_.empty()) {
        return generated;
    }
    const int x = x_.first(i);
    const int y = y_.first(j);
    const int z = z_.first(k);
    NearestCubes nearest;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            const std::size_t row = at(x, y + dy + 1, z + dz + 1);
            for (int dx = -1; dx <= 1; ++dx) {
                nearest.add(states_[row + static_cast<std::size_t>(dx + 1)],
                            offsetIndex(dx, dy, dz));
            }
        }
    }
    return nearest.apply(generated);
}

BlockCoord plantFootBlock(const Plant& plant) {
    return {static_cast<std::int32_t>(std::floor(plant.x + 0.5)),
            static_cast<std::int32_t>(std::floor(static_cast<double>(plant.y))),
            static_cast<std::int32_t>(std::floor(plant.z + 0.5))};
}

void dropPlantsOnRemovedGround(const EditWindow& window, std::vector<Plant>& plants) {
    if (window.empty()) {
        return;
    }
    std::erase_if(plants, [&window](const Plant& plant) {
        const BlockCoord foot = plantFootBlock(plant);
        return window.blockAt(foot.x, foot.y, foot.z) == BlockEdit::Removed;
    });
}

} // namespace terrain
