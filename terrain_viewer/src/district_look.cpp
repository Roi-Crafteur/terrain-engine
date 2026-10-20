#include "district_look.hpp"

#include "cave_look.hpp"
#include "terrain/io/cave_regions_png.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace viewer {

namespace {

constexpr Vec3 kNeutralGray{0.5f, 0.5f, 0.5f};

// The label lists at most this many district types.
constexpr int kMaxLabelEntries = 3;

// The mix of the districts is sampled on a grid over the vertices of a tile: cells of about
// kMixSpacing meters, at most kMixMaxCells per axis. The mix changes over hundreds of meters (the
// blend of the districts is 280 m wide by default), so the bilinear interpolation of the samples
// is as good as one lookup per vertex, with a hundred times fewer.
constexpr double kMixSpacing = 8.0;
constexpr int kMixMaxCells = 8;

// One axis of the grid: cells of equal size from min; step 0 when every vertex is on one line.
struct Axis {
    double min = 0.0;
    double step = 0.0;
    int cells = 1;

    [[nodiscard]] double at(int index) const { return min + step * index; }
};

// A position on an axis: the cell it lies in and how far along it, in [0, 1].
struct CellPosition {
    int cell = 0;
    float fraction = 0.0f;
};

Axis makeAxis(double low, double high) {
    const double extent = std::max(high - low, 0.0);
    const int wanted = static_cast<int>(std::ceil(extent / kMixSpacing));
    const int cells = std::clamp(wanted, 1, kMixMaxCells);
    return {low, extent / cells, cells};
}

CellPosition locate(const Axis& axis, double value) {
    if (axis.step <= 0.0) {
        return {};
    }
    const double along = std::clamp((value - axis.min) / axis.step, 0.0,
                                    static_cast<double>(axis.cells));
    const int cell = std::min(static_cast<int>(along), axis.cells - 1);
    return {cell, static_cast<float>(along - cell)};
}

// The district mix on a grid over a rectangle, interpolated bilinearly.
class MixGrid {
public:
    MixGrid(const Axis& x, const Axis& z, const DistrictColorAt& mixAt) : x_(x), z_(z) {
        samples_.reserve(static_cast<std::size_t>((x.cells + 1) * (z.cells + 1)));
        for (int j = 0; j <= z.cells; ++j) {
            for (int i = 0; i <= x.cells; ++i) {
                samples_.push_back(mixAt(x.at(i), z.at(j)));
            }
        }
    }

    [[nodiscard]] Vec3 at(double x, double z) const {
        const CellPosition a = locate(x_, x);
        const CellPosition b = locate(z_, z);
        const Vec3 lower = lerp(sample(a.cell, b.cell), sample(a.cell + 1, b.cell), a.fraction);
        const Vec3 upper =
            lerp(sample(a.cell, b.cell + 1), sample(a.cell + 1, b.cell + 1), a.fraction);
        return lerp(lower, upper, b.fraction);
    }

private:
    [[nodiscard]] Vec3 sample(int i, int j) const {
        return samples_[static_cast<std::size_t>(j * (x_.cells + 1) + i)];
    }

    Axis x_;
    Axis z_;
    std::vector<Vec3> samples_;
};

// World position of a vertex of the tile.
struct WorldXZ {
    double x;
    double z;
};

WorldXZ worldXZ(const terrain::TileData& data, std::size_t vertex) {
    const terrain::Vec3f p = data.mesh.vertices[vertex];
    return {data.originX + static_cast<double>(p.x), data.originZ + static_cast<double>(p.z)};
}

bool isRock(const terrain::TileData& data, std::size_t vertex) {
    return data.materials[vertex] != terrain::CaveMaterial::Surface;
}

// The grid over the vertices below the outer surface, or no grid if there are none.
std::optional<MixGrid> rockGrid(const terrain::TileData& data, const DistrictColorAt& mixAt) {
    bool any = false;
    WorldXZ low{0.0, 0.0};
    WorldXZ high{0.0, 0.0};
    for (std::size_t i = 0; i < data.materials.size(); ++i) {
        if (!isRock(data, i)) {
            continue;
        }
        const WorldXZ p = worldXZ(data, i);
        low = any ? WorldXZ{std::min(low.x, p.x), std::min(low.z, p.z)} : p;
        high = any ? WorldXZ{std::max(high.x, p.x), std::max(high.z, p.z)} : p;
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return MixGrid(makeAxis(low.x, high.x), makeAxis(low.z, high.z), mixAt);
}

std::string capitalized(const char* name) {
    std::string text = name;
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

// The weight of a district type in a mix.
struct TypeShare {
    terrain::CaveDistrictType type;
    float weight;
};

// The shares of the types of a mix, largest first; the nearest district first among equals.
std::vector<TypeShare> typeShares(const terrain::DistrictWeights& weights) {
    std::vector<TypeShare> shares;
    const int count = std::clamp(weights.count, 0, static_cast<int>(weights.districts.size()));
    for (int i = 0; i < count; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        const terrain::CaveDistrictType type = weights.districts[slot].type;
        const auto same = std::ranges::find(shares, type, &TypeShare::type);
        if (same != shares.end()) {
            same->weight += weights.weights[slot];
        } else {
            shares.push_back({type, weights.weights[slot]});
        }
    }
    std::ranges::stable_sort(shares, std::greater<>(), &TypeShare::weight);
    return shares;
}

} // namespace

Vec3 caveDistrictColor(terrain::CaveDistrictType type) {
    const std::array<std::uint8_t, 3> rgb = terrain::caveDistrictRgb(type);
    return {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f};
}

Vec3 districtMixColor(const terrain::DistrictWeights& weights) {
    Vec3 sum;
    float total = 0.0f;
    const int count = std::clamp(weights.count, 0, static_cast<int>(weights.districts.size()));
    for (int i = 0; i < count; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        sum = sum + caveDistrictColor(weights.districts[slot].type) * weights.weights[slot];
        total += weights.weights[slot];
    }
    return total > 0.0f ? sum * (1.0f / total) : kNeutralGray;
}

Vec3 tintColor(Vec3 material, Vec3 mix, float strength) {
    return lerp(material, mix, strength);
}

std::vector<Vec3> districtRockColors(const terrain::TileData& data, const DistrictColorAt& mixAt,
                                     float strength) {
    std::vector<Vec3> colors = rockColors(data);
    if (!mixAt || data.materials.size() != colors.size()) {
        return colors;
    }
    const std::optional<MixGrid> grid = rockGrid(data, mixAt);
    if (!grid) {
        return colors;
    }
    for (std::size_t i = 0; i < colors.size(); ++i) {
        if (isRock(data, i)) {
            const WorldXZ p = worldXZ(data, i);
            colors[i] = tintColor(colors[i], grid->at(p.x, p.z), strength);
        }
    }
    return colors;
}

std::string districtMixLabel(const terrain::DistrictWeights& weights) {
    std::string label;
    int shown = 0;
    for (const TypeShare& share : typeShares(weights)) {
        const long percent = std::lround(static_cast<double>(share.weight) * 100.0);
        if (shown == kMaxLabelEntries || percent <= 0) {
            break; // the shares are sorted: the next ones are smaller
        }
        if (shown > 0) {
            label += " / ";
        }
        label += std::format("{} {}%", capitalized(terrain::caveDistrictName(share.type)), percent);
        ++shown;
    }
    return label;
}

} // namespace viewer
