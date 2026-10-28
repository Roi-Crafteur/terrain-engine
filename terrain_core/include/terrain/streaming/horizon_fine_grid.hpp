#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_levels.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain {

// Heights of the fine lattice of a level of the far horizon, `ratio` times finer than its samples:
// node (u, v) lies at (u, v) * pitch / ratio, read at its canonical position (wrapIndex on the
// samples of the level times the ratio), as Heightfield::heightAt gives it (no player edit).
// Node (i * ratio, k * ratio) is the sample (i, k), and a lattice `ratio` times finer than another
// holds its nodes every ratio / other nodes, at the same positions: the same bits. The detail
// slopes (horizon_normals.hpp) and the crests (horizon_crests.hpp) of a rectangle read their
// heights from one grid, read once (horizon_detail.hpp).
//
// A grid holds a box of nodes (fine indices, continuous as the samples of the windows); only the
// nodes asked for are read, the others hold 0.
class HorizonFineGrid {
public:
    // The nodes [box.x0, box.x1) x [box.z0, box.z1) of a level, none read yet. Throws
    // std::invalid_argument for invalid parameters (validate), a level out of range, or a ratio
    // below 1 or not dividing the pitch of the level.
    HorizonFineGrid(const HorizonParams& params, int level, int ratio, const HorizonRect& box);

    [[nodiscard]] int level() const { return level_; }
    [[nodiscard]] int ratio() const { return ratio_; }
    [[nodiscard]] int spacing() const { return spacing_; } // meters between two nodes
    [[nodiscard]] const HorizonRect& box() const { return box_; }
    [[nodiscard]] bool covers(const HorizonRect& nodes) const {
        return nodes.empty() || intersection(nodes, box_) == nodes;
    }

    // Height of a node of the box, unchecked.
    [[nodiscard]] float at(std::int64_t u, std::int64_t v) const {
        return heights_[index(u, v)];
    }
    void set(std::int64_t u, std::int64_t v, float height) { heights_[index(u, v)] = height; }

    // Reads every node of a rectangle of the box: one Heightfield::heightsAt per row, in order.
    // field must share the world of the parameters. Throws std::invalid_argument for a rectangle
    // outside the box.
    void read(const Heightfield& field, const HorizonRect& nodes);
    // Reads the nodes of the box for which wanted(u, v) holds: one Heightfield::heightsAt per row
    // that has some.
    template <typename Wanted>
    void readIf(const Heightfield& field, Wanted&& wanted) {
        std::vector<std::int64_t> columns;
        for (std::int64_t v = box_.z0; v < box_.z1; ++v) {
            columns.clear();
            for (std::int64_t u = box_.x0; u < box_.x1; ++u) {
                if (wanted(u, v)) {
                    columns.push_back(u);
                }
            }
            readRow(field, v, columns);
        }
    }

    [[nodiscard]] std::size_t memoryBytes() const { return heights_.size() * sizeof(float); }

private:
    [[nodiscard]] std::size_t index(std::int64_t u, std::int64_t v) const {
        return static_cast<std::size_t>((v - box_.z0) * box_.width() + (u - box_.x0));
    }
    void readRow(const Heightfield& field, std::int64_t v, std::span<const std::int64_t> columns);

    int level_;
    int ratio_;
    int spacing_;
    HorizonIndex counts_; // nodes along each side of the world
    HorizonRect box_;
    std::vector<float> heights_;
};

} // namespace terrain
