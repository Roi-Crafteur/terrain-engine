#include "tile_water_mesh.hpp"

#include "terrain/det_math.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

#include <array>
#include <cmath>
#include <cstddef>
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

// The slope of the water at each wet column, from its wet neighbors (centered, or one-sided).
void slopes(std::vector<Column>& columns, double spacing) {
    const auto slope = [&](int i, int k, int di, int dk) {
        const bool before = i - di >= 0 && k - dk >= 0 && columns[at(i - di, k - dk)].wet;
        const bool after = i + di <= kCells && k + dk <= kCells && columns[at(i + di, k + dk)].wet;
        const double here = columns[at(i, k)].level;
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

// Builds the vertices and triangles of the cells.
class Builder {
public:
    Builder(const TileData& data, std::vector<Column> columns, double spacing)
        : columns_(std::move(columns)), spacing_(spacing), originY_(data.originY),
          shared_(columns_.size(), -1) {}

    void cell(int i, int k) {
        const std::array<std::size_t, 4> corners{at(i, k), at(i + 1, k), at(i, k + 1),
                                                 at(i + 1, k + 1)};
        Column mean;
        int wet = 0;
        for (const std::size_t c : corners) {
            if (columns_[c].wet) {
                add(mean, columns_[c], wet == 0);
                ++wet;
            }
        }
        if (wet == 0) {
            return;
        }
        scale(mean, 1.0 / wet);
        std::array<double, 4> levels{};
        for (std::size_t n = 0; n < corners.size(); ++n) {
            levels[n] = columns_[corners[n]].wet ? columns_[corners[n]].level : mean.level;
        }
        const double limit = kWaterCutSlope * spacing_;
        if (std::abs(levels[0] - levels[1]) > limit || std::abs(levels[2] - levels[3]) > limit ||
            std::abs(levels[0] - levels[2]) > limit || std::abs(levels[1] - levels[3]) > limit) {
            return; // the face of a fall
        }
        std::array<std::int32_t, 4> v{};
        const std::array<std::array<int, 2>, 4> offsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
        for (std::size_t n = 0; n < corners.size(); ++n) {
            const int ci = i + offsets[n][0];
            const int ck = k + offsets[n][1];
            v[n] = columns_[corners[n]].wet ? sharedVertex(ci, ck) : vertex(ci, ck, mean);
        }
        // Front faces up (Unreal: the side of (c - a) x (b - a)).
        for (const std::int32_t index : {v[0], v[3], v[1], v[0], v[2], v[3]}) {
            mesh.triangles.push_back(index);
        }
    }

    UnrealWaterMesh mesh;

private:
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

    std::int32_t vertex(int i, int k, const Column& c) {
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
        mesh.uv2.push_back({static_cast<float>(c.level), 0.0f});
        return index;
    }

    std::vector<Column> columns_;
    double spacing_;
    double originY_;
    std::vector<std::int32_t> shared_;
};

} // namespace

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
