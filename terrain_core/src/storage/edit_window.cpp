#include "terrain/storage/edit_window.hpp"

#include "edit_cubes.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

using edit_cubes::NearestCubes;
using edit_cubes::offsetIndex;
using edit_cubes::stateOf;

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

std::optional<std::pair<int, int>> EditWindow::editedSpan(int minX, int maxX, int minZ,
                                                          int maxZ) const {
    minX = std::max(minX, minX_);
    maxX = std::min(maxX, maxX_);
    minZ = std::max(minZ, minZ_);
    maxZ = std::min(maxZ, maxZ_);
    std::optional<std::pair<int, int>> span;
    for (std::int32_t cz = minZ >> kSectionShift; cz <= maxZ >> kSectionShift; ++cz) {
        for (std::int32_t cx = minX >> kSectionShift; cx <= maxX >> kSectionShift; ++cx) {
            const Column* found = column(cx, cz);
            if (found == nullptr) {
                continue;
            }
            const int x0 = cx * kSectionSide;
            const int z0 = cz * kSectionSide;
            const int lx0 = std::max(minX, x0) - x0;
            const int lx1 = std::min(maxX, x0 + kSectionSide - 1) - x0;
            const int lz0 = std::max(minZ, z0) - z0;
            const int lz1 = std::min(maxZ, z0 + kSectionSide - 1) - z0;
            for (std::size_t s = 0; s < found->ys.size(); ++s) {
                if (const auto layers = found->sections[s].editedLayers(lx0, lx1, lz0, lz1)) {
                    const int base = found->ys[s] * kSectionSide;
                    const int low = base + layers->first;
                    const int high = base + layers->second;
                    span = std::pair{span ? std::min(span->first, low) : low,
                                     span ? std::max(span->second, high) : high};
                }
            }
        }
    }
    return span;
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

bool EditWindow::holds(BlockEdit edit, BlockCoord min, BlockCoord max) const {
    for (std::int32_t cz = min.z >> kSectionShift; cz <= max.z >> kSectionShift; ++cz) {
        for (std::int32_t cx = min.x >> kSectionShift; cx <= max.x >> kSectionShift; ++cx) {
            const Column* found = column(cx, cz);
            if (found == nullptr) {
                continue;
            }
            auto it = std::lower_bound(found->ys.begin(), found->ys.end(), sectionOf(min.y));
            for (; it != found->ys.end() && *it <= sectionOf(max.y); ++it) {
                const auto s = static_cast<std::size_t>(it - found->ys.begin());
                if (found->sections[s].count(edit) > 0) {
                    return true;
                }
            }
        }
    }
    return false;
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
