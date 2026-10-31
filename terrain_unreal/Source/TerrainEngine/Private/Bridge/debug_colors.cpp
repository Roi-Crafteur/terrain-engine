#include "debug_colors.hpp"

#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace terrain::unreal {

namespace {

// The cosines of 15, 30, 45, 60 and 80 degrees: the classes of the slopes, by the vertical
// component of the normal.
constexpr std::array<float, 5> kSlopeCosines = {0.96593f, 0.86603f, 0.70711f, 0.5f, 0.17365f};
constexpr std::array<VertexColor, 7> kSlopeColors = {{
    {90, 170, 80, 255},  // under 15 degrees
    {170, 200, 70, 255}, // to 30
    {240, 210, 60, 255}, // to 45
    {240, 140, 50, 255}, // to 60
    {220, 60, 50, 255},  // to 80
    {200, 60, 200, 255}, // to vertical
    {60, 90, 230, 255},  // overhang
}};
constexpr VertexColor kOnHeightmap{150, 150, 150, 255};
constexpr VertexColor kOffHeightmap{255, 120, 0, 255};
constexpr double kSideEpsilonCm = 0.01;

std::uint8_t unitToByte(float v) {
    const float share = std::clamp((v + 1.0f) * 0.5f, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(share * 255.0f));
}

} // namespace

VertexColor lodColor(int lod) {
    constexpr std::array<VertexColor, kLodCount> kColors = {{
        {230, 80, 60, 255},
        {240, 200, 60, 255},
        {70, 180, 90, 255},
        {70, 120, 230, 255},
    }};
    return kColors[static_cast<std::size_t>(std::clamp(lod, 0, kLodCount - 1))];
}

VertexColor slopeColor(const UnrealVector3f& normal) {
    if (normal.z < 0.0f) {
        return kSlopeColors.back();
    }
    std::size_t index = 0;
    while (index < kSlopeCosines.size() && normal.z < kSlopeCosines[index]) {
        ++index;
    }
    return kSlopeColors[index];
}

VertexColor normalColor(const UnrealVector3f& normal) {
    return {unitToByte(normal.x), unitToByte(normal.y), unitToByte(normal.z), 255};
}

std::vector<float> tileLatticeHeights(const Heightfield& field, int lod, double x, double z) {
    const double cell = tileCellMeters(lod);
    std::vector<double> xs;
    std::vector<double> zs;
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            xs.push_back(x + i * cell);
            zs.push_back(z + k * cell);
        }
    }
    std::vector<float> heights(xs.size());
    field.heightsAt(xs, zs, heights);
    return heights;
}

std::vector<std::uint8_t> offHeightmap(const UnrealMesh& mesh, int lod, const CorePoint& origin,
                                       std::span<const float> lattice) {
    const std::size_t samples = static_cast<std::size_t>(kGridSamples);
    if (lattice.size() != samples * samples) {
        throw std::invalid_argument("offHeightmap: the lattice of a tile expected");
    }
    const double cell = tileCellMeters(lod);
    const double sideCm = tileSizeMeters(lod) * kCentimetersPerMeter;
    const double tolerance = std::max(0.5, 0.5 * cell);
    std::vector<std::uint8_t> off(mesh.positions.size(), 0);
    for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
        const UnrealVector3f& p = mesh.positions[v];
        if (p.x <= kSideEpsilonCm || p.y <= kSideEpsilonCm || p.x >= sideCm - kSideEpsilonCm ||
            p.y >= sideCm - kSideEpsilonCm) {
            continue;
        }
        const double u = p.x / kCentimetersPerMeter / cell; // Unreal X: core x
        const double w = p.y / kCentimetersPerMeter / cell; // Unreal Y: core z
        const double last = static_cast<double>(samples) - 2.0; // the last cell
        const auto i = static_cast<std::size_t>(std::clamp(std::floor(u), 0.0, last));
        const auto k = static_cast<std::size_t>(std::clamp(std::floor(w), 0.0, last));
        const double fu = u - static_cast<double>(i);
        const double fw = w - static_cast<double>(k);
        const auto at = [&](std::size_t a, std::size_t b) {
            return static_cast<double>(lattice[b * samples + a]);
        };
        const double h = (at(i, k) * (1.0 - fu) + at(i + 1, k) * fu) * (1.0 - fw) +
                         (at(i, k + 1) * (1.0 - fu) + at(i + 1, k + 1) * fu) * fw;
        const double altitude = origin.y + p.z / kCentimetersPerMeter;
        off[v] = std::abs(altitude - h) > tolerance ? 1 : 0;
    }
    return off;
}

std::vector<VertexColor> debugColors(ViewMode mode, const UnrealMesh& mesh, int lod,
                                     std::span<const std::uint8_t> off) {
    std::vector<VertexColor> colors(mesh.positions.size());
    for (std::size_t v = 0; v < colors.size(); ++v) {
        switch (mode) {
        case ViewMode::Lod: colors[v] = lodColor(lod); break;
        case ViewMode::Slope: colors[v] = slopeColor(mesh.normals[v]); break;
        case ViewMode::Normals: colors[v] = normalColor(mesh.normals[v]); break;
        case ViewMode::Rock:
            colors[v] = v < off.size() && off[v] != 0 ? kOffHeightmap : kOnHeightmap;
            break;
        default: colors[v] = v < mesh.colors.size() ? mesh.colors[v] : VertexColor{}; break;
        }
    }
    return colors;
}

bool colorsVertices(ViewMode mode) {
    return mode == ViewMode::Lod || mode == ViewMode::Slope || mode == ViewMode::Normals ||
           mode == ViewMode::Rock;
}

} // namespace terrain::unreal
