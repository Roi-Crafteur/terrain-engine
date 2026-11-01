#include "tile_water_mesh.hpp"

#include "fall_edges.hpp"

#include "terrain/det_math.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace terrain::unreal {

namespace {

constexpr int kSide = kGridSamples;
constexpr int kCells = kChunkCells;

[[nodiscard]] constexpr std::size_t at(int i, int k) {
    return static_cast<std::size_t>(k * kSide + i);
}

float kindCode(WaterKind kind) {
    switch (kind) {
    case WaterKind::River:
        return kWaterRiver;
    case WaterKind::Lake:
        return kWaterLake;
    case WaterKind::SaltLake:
        return kWaterSaltLake;
    case WaterKind::None:
    case WaterKind::Ocean:
        break;
    }
    return 0.0f;
}

// A column as the surface takes it: under a river or a lake, its level (meters) and its flow.
struct Column {
    bool wet = false;
    double level = 0.0;
    float kind = 0.0f;
    float flowX = 0.0f;
    float flowZ = 0.0f;
    double slopeX = 0.0; // of the water, meters per meter, from the wet neighbors
    double slopeZ = 0.0;
};

// The odd columns of each side stitched to a coarser tile, between two wet even ones, at their
// mean level: the straight edge of the cell of the coarser tile.
void stitch(std::vector<Column>& columns, unsigned seams) {
    struct Side {
        unsigned seam;
        bool alongZ; // the side runs along z (i fixed)
        int fixed;
    };
    constexpr std::array<Side, 4> kSides{{{kSeamNegX, true, 0},
                                          {kSeamPosX, true, kCells},
                                          {kSeamNegZ, false, 0},
                                          {kSeamPosZ, false, kCells}}};
    for (const Side& side : kSides) {
        if ((seams & side.seam) == 0) {
            continue;
        }
        const auto index = [&](int j) { return side.alongZ ? at(side.fixed, j) : at(j, side.fixed); };
        for (int j = 1; j < kCells; j += 2) {
            const Column& a = columns[index(j - 1)];
            const Column& b = columns[index(j + 1)];
            Column& c = columns[index(j)];
            if (a.wet && b.wet) {
                if (!c.wet) {
                    c = a;
                }
                c.level = 0.5 * (a.level + b.level);
            }
        }
    }
}

// The slope of the water at each wet column, from its wet neighbors (centered, or one-sided),
// never across the face of a fall (a step steeper than kWaterCutSlope).
void slopes(std::vector<Column>& columns, double spacing) {
    const double limit = kWaterCutSlope * spacing;
    const auto near = [&](int i, int k, double here) {
        const Column& c = columns[at(i, k)];
        return c.wet && std::abs(c.level - here) <= limit;
    };
    const auto slope = [&](int i, int k, int di, int dk) {
        const double here = columns[at(i, k)].level;
        const bool before = i - di >= 0 && k - dk >= 0 && near(i - di, k - dk, here);
        const bool after = i + di <= kCells && k + dk <= kCells && near(i + di, k + dk, here);
        const double low = before ? columns[at(i - di, k - dk)].level : here;
        const double high = after ? columns[at(i + di, k + dk)].level : here;
        const int steps = (before ? 1 : 0) + (after ? 1 : 0);
        return steps == 0 ? 0.0 : (high - low) / (steps * spacing);
    };
    for (int k = 0; k <= kCells; ++k) {
        for (int i = 0; i <= kCells; ++i) {
            Column& c = columns[at(i, k)];
            if (c.wet) {
                c.slopeX = slope(i, k, 1, 0);
                c.slopeZ = slope(i, k, 0, 1);
            }
        }
    }
}

std::vector<Column> columnsOf(const TileData& data, double spacing) {
    std::vector<Column> columns(static_cast<std::size_t>(kSide * kSide));
    for (std::size_t n = 0; n < columns.size(); ++n) {
        const WaterColumn& w = data.waterColumns[n];
        const float kind = kindCode(w.kind);
        if (kind > 0.0f && w.level > kNoWaterLevel) {
            columns[n] = {true, static_cast<double>(w.level), kind, w.flowX, w.flowZ, 0.0, 0.0};
        }
    }
    stitch(columns, data.request.seams);
    slopes(columns, spacing);
    return columns;
}

// A cell with a wet corner: the levels of its corners (the dry ones at the mean level of the wet
// ones), that mean, and whether it is cut (the face of a fall).
struct Cell {
    bool drawn = false;
    bool cut = false;
    Column mean;
    std::array<double, 4> levels{};
};

constexpr std::array<std::array<int, 2>, 4> kCorners{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};

// The water of a corner is that above (or below) a fall within kLevelLeast meters plus
// kLevelCells cells of its level, near its edge within kReachCells cells (and a meter).
constexpr double kLevelLeast = 0.25;
constexpr double kLevelCells = 0.35;
constexpr double kReachCells = 3.0;

// Builds the vertices and triangles of the cells.
class Builder {
public:
    Builder(const TileData& data, std::vector<Column> columns, double spacing)
        : columns_(std::move(columns)), spacing_(spacing), originY_(data.originY),
          shared_(columns_.size(), -1), edges_(data) {}

    void cell(int i, int k) {
        const Cell c = describe(i, k);
        if (c.drawn && c.cut) {
            ++mesh.cutCells;
            if (!cutCell(i, k)) {
                highCell(i, k);
            }
            poolCell(i, k);
        }
        if (!c.drawn || c.cut) {
            return;
        }
        std::array<std::int32_t, 4> v{};
        for (std::size_t n = 0; n < kCorners.size(); ++n) {
            const int ci = i + kCorners[n][0];
            const int ck = k + kCorners[n][1];
            v[n] = columns_[at(ci, ck)].wet ? sharedVertex(ci, ck) : vertex(ci, ck, c.mean);
        }
        // Front faces up (Unreal: the side of (c - a) x (b - a)).
        for (const std::int32_t index : {v[0], v[3], v[1], v[0], v[2], v[3]}) {
            mesh.triangles.push_back(index);
        }
    }

    UnrealWaterMesh mesh;

private:
    [[nodiscard]] Cell describe(int i, int k) const {
        Cell cell;
        int wet = 0;
        for (const auto& o : kCorners) {
            const Column& c = columns_[at(i + o[0], k + o[1])];
            if (c.wet) {
                add(cell.mean, c, wet == 0);
                ++wet;
            }
        }
        if (wet == 0) {
            return cell;
        }
        scale(cell.mean, 1.0 / wet);
        for (std::size_t n = 0; n < kCorners.size(); ++n) {
            const Column& c = columns_[at(i + kCorners[n][0], k + kCorners[n][1])];
            cell.levels[n] = c.wet ? c.level : cell.mean.level;
        }
        const std::array<double, 4>& l = cell.levels;
        const double limit = kWaterCutSlope * spacing_;
        cell.drawn = true;
        cell.cut = std::abs(l[0] - l[1]) > limit || std::abs(l[2] - l[3]) > limit ||
                   std::abs(l[0] - l[2]) > limit || std::abs(l[1] - l[3]) > limit;
        return cell;
    }

    static void add(Column& sum, const Column& c, bool first) {
        sum.level += c.level;
        sum.flowX += c.flowX;
        sum.flowZ += c.flowZ;
        sum.slopeX += c.slopeX;
        sum.slopeZ += c.slopeZ;
        sum.kind = first ? c.kind : sum.kind;
    }

    static void scale(Column& sum, double share) {
        sum.level *= share;
        sum.slopeX *= share;
        sum.slopeZ *= share;
        const double flow = dmath::hypot(sum.flowX, sum.flowZ);
        sum.flowX = flow > 0.0 ? static_cast<float>(sum.flowX / flow) : 0.0f;
        sum.flowZ = flow > 0.0 ? static_cast<float>(sum.flowZ / flow) : 0.0f;
    }

    std::int32_t sharedVertex(int i, int k) {
        std::int32_t& index = shared_[at(i, k)];
        if (index < 0) {
            index = vertex(i, k, columns_[at(i, k)]);
        }
        return index;
    }

    // The fade of the water of corner (i, k) at level: near the edge of the lip of a fall whose
    // water above stands there, by its distance to it (whether a cut cell of the tile meets the
    // corner or not: the same in the tile beside); none elsewhere (a cut off the falls fades its
    // water within its cell: highCell).
    [[nodiscard]] double fadeAt(int i, int k, double level) const {
        if (edges_.size() == 0) {
            return 1.0;
        }
        const double tolerance = kLevelLeast + kLevelCells * spacing_;
        const double reach = kReachCells * spacing_ + 1.0;
        return edges_.above(i * spacing_, k * spacing_, level, tolerance, reach).value_or(1.0);
    }

    // A cut cell across the edge of the lip of a fall, a corner of it under the water above the
    // fall and upstream of the edge: drawn at that water (its corners there at their level, the
    // others at the level of the fall above: the same on both sides of a side of a tile), faded
    // out to nothing at the edge (its corners beyond it below 0): the water ends at the edge, not
    // at a side of a cell.
    bool cutCell(int i, int k) {
        const double tolerance = kLevelLeast + kLevelCells * spacing_;
        const auto above = [&](const Column& c, std::size_t n) {
            return c.wet && edges_.upper(n, c.level, tolerance);
        };
        for (std::size_t n = 0; n < edges_.size(); ++n) {
            const Column* top = nullptr;
            bool upstream = false;
            for (const auto& o : kCorners) {
                const Column& c = columns_[at(i + o[0], k + o[1])];
                if (above(c, n)) {
                    const std::optional<double> fade =
                        edges_.toward(n, (i + o[0]) * spacing_, (k + o[1]) * spacing_);
                    upstream = upstream || (fade && *fade > 0.0);
                    top = top == nullptr || c.level > top->level ? &c : top;
                }
            }
            if (!upstream) {
                continue;
            }
            Column fall = *top;
            fall.level = edges_.top(n);
            std::array<std::int32_t, 4> v{};
            for (std::size_t c = 0; c < kCorners.size(); ++c) {
                const int ci = i + kCorners[c][0];
                const int ck = k + kCorners[c][1];
                const Column& own = columns_[at(ci, ck)];
                const std::optional<double> fade = edges_.toward(n, ci * spacing_, ck * spacing_);
                v[c] = vertex(ci, ck, above(own, n) ? own : fall, fade ? *fade : kEdgeBeyond);
            }
            for (const std::int32_t index : {v[0], v[3], v[1], v[0], v[2], v[3]}) {
                mesh.triangles.push_back(index);
            }
            return true;
        }
        return false;
    }

    // A cut cell off the falls (no edge of a lip across it) drawn at its highest water: its
    // corners within kLevelLeast plus kLevelCells cells of it at their level and shown, the others
    // at its level and faded out: the water above the cut ends within the cell, never at a side
    // of it, and never leaves the bed bare before the cut.
    void highCell(int i, int k) {
        const double tolerance = kLevelLeast + kLevelCells * spacing_;
        const Column* highest = nullptr;
        for (const auto& o : kCorners) {
            const Column& c = columns_[at(i + o[0], k + o[1])];
            highest = c.wet && (highest == nullptr || c.level > highest->level) ? &c : highest;
        }
        if (highest == nullptr) {
            return;
        }
        std::array<std::int32_t, 4> v{};
        for (std::size_t c = 0; c < kCorners.size(); ++c) {
            const int ci = i + kCorners[c][0];
            const int ck = k + kCorners[c][1];
            const Column& own = columns_[at(ci, ck)];
            const bool high = own.wet && highest->level - own.level <= tolerance;
            v[c] = high ? sharedVertex(ci, ck) : vertex(ci, ck, *highest, 0.0);
        }
        for (const std::int32_t index : {v[0], v[3], v[1], v[0], v[2], v[3]}) {
            mesh.triangles.push_back(index);
        }
    }

    // A cut cell drawn at its lowest water too (its corners within kLevelLeast plus kLevelCells
    // cells of it at their level, the others at its level, under the face or the curtain), whole:
    // the pool below a face goes on to it, never a square of bare bed beside the foot of a
    // curtain.
    void poolCell(int i, int k) {
        const double tolerance = kLevelLeast + kLevelCells * spacing_;
        const Column* lowest = nullptr;
        for (const auto& o : kCorners) {
            const Column& c = columns_[at(i + o[0], k + o[1])];
            lowest = c.wet && (lowest == nullptr || c.level < lowest->level) ? &c : lowest;
        }
        if (lowest == nullptr) {
            return;
        }
        std::array<std::int32_t, 4> v{};
        for (std::size_t c = 0; c < kCorners.size(); ++c) {
            const int ci = i + kCorners[c][0];
            const int ck = k + kCorners[c][1];
            const Column& own = columns_[at(ci, ck)];
            const bool pool = own.wet && own.level - lowest->level <= tolerance;
            v[c] = vertex(ci, ck, pool ? own : *lowest, 1.0);
        }
        for (const std::int32_t index : {v[0], v[3], v[1], v[0], v[2], v[3]}) {
            mesh.triangles.push_back(index);
        }
    }

    std::int32_t vertex(int i, int k, const Column& c) {
        return vertex(i, k, c, fadeAt(i, k, c.level));
    }

    std::int32_t vertex(int i, int k, const Column& c, double fade) {
        const auto index = static_cast<std::int32_t>(mesh.positions.size());
        const double x = i * spacing_;
        const double z = k * spacing_;
        mesh.positions.push_back({static_cast<float>(kCentimetersPerMeter * x),
                                  static_cast<float>(kCentimetersPerMeter * z),
                                  static_cast<float>(kCentimetersPerMeter * (c.level - originY_))});
        const double length = dmath::hypot(dmath::hypot(c.slopeX, c.slopeZ), 1.0);
        mesh.normals.push_back({static_cast<float>(-c.slopeX / length),
                                static_cast<float>(-c.slopeZ / length),
                                static_cast<float>(1.0 / length)});
        mesh.uv0.push_back({c.flowX, c.flowZ});
        mesh.uv1.push_back({c.kind, static_cast<float>(dmath::hypot(c.slopeX, c.slopeZ))});
        mesh.uv2.push_back({static_cast<float>(c.level), static_cast<float>(fade)});
        return index;
    }

    std::vector<Column> columns_;
    double spacing_;
    double originY_;
    std::vector<std::int32_t> shared_;
    FallEdges edges_;
};

} // namespace

std::vector<double> drawnWaterLevels(const TileData& data) {
    if (data.waterColumns.size() != static_cast<std::size_t>(kSide * kSide)) {
        return {};
    }
    const std::vector<Column> columns =
        columnsOf(data, tileCellMeters(data.request.key.lod));
    std::vector<double> levels(columns.size(), static_cast<double>(kNoWaterLevel));
    for (std::size_t n = 0; n < columns.size(); ++n) {
        levels[n] = columns[n].wet ? columns[n].level : levels[n];
    }
    return levels;
}

UnrealWaterMesh buildTileWater(const TileData& data) {
    if (data.waterColumns.size() != static_cast<std::size_t>(kSide * kSide)) {
        return {};
    }
    const double spacing = tileCellMeters(data.request.key.lod);
    Builder builder(data, columnsOf(data, spacing), spacing);
    for (int k = 0; k < kCells; ++k) {
        for (int i = 0; i < kCells; ++i) {
            builder.cell(i, k);
        }
    }
    return std::move(builder.mesh);
}

} // namespace terrain::unreal
