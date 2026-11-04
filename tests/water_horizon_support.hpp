#pragma once

// A level of the GPU horizon as Unreal draws it (agent 40), on the default world with its planet
// file (coast_planet_support.hpp): the samples of a rectangle of the level, raised to their crests
// as the GPU raises them (HorizonCrestSampler at the ratio of the GPU, crestHeight), turned into
// the texels the material reads (horizonTexel), and what its vertex shader and its triangles make
// of them: the ground shown at a point and the normal of the grid there, linear on the triangles
// of a cell (horizon_split.hpp), or bilinear over the cell as the tests of agent 33 read it.

#include "coast_planet_support.hpp"

#include "horizon_crest_texels.hpp"
#include "horizon_sea_line.hpp"
#include "horizon_split.hpp"
#include "horizon_textures.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/streaming/horizon_detail.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace water_horizon {

using terrain::HorizonRect;
using terrain::unreal::CellSplit;
using terrain::unreal::HorizonTexel;
using terrain::unreal::SeaDrawing;

// The ratios of the crests and of the detail normals of the GPU horizon
// (Bridge/horizon_gpu_streamer.hpp: crestRatio, normalRatio).
inline constexpr int kGpuCrestRatio = 4;
inline constexpr int kGpuNormalRatio = 4;

inline const terrain::HorizonParams& params() {
    static const terrain::HorizonParams p{};
    return p;
}

// The samples of a rectangle of a level as the GPU horizon draws them.
struct DrawnLevel {
    int level = 0;
    double pitch = 0.0;
    HorizonRect rect;
    std::vector<HorizonTexel> texels; // row by row, x fastest
    // The detail slopes, kGpuNormalRatio per sample along each axis (HorizonNormalSampler).
    std::vector<terrain::DetailSlope> detail;

    [[nodiscard]] const HorizonTexel& at(std::int64_t i, std::int64_t k) const {
        return texels[static_cast<std::size_t>(k * rect.width() + i)];
    }
    // The cell holding (x, z): its corner (i, k) in the rectangle and (u, v) within it, if inside.
    [[nodiscard]] bool cellOf(double x, double z, std::int64_t& i, std::int64_t& k, double& u,
                              double& v) const {
        const double fu = x / pitch - static_cast<double>(rect.x0);
        const double fv = z / pitch - static_cast<double>(rect.z0);
        i = static_cast<std::int64_t>(std::floor(fu));
        k = static_cast<std::int64_t>(std::floor(fv));
        u = fu - static_cast<double>(i);
        v = fv - static_cast<double>(k);
        return i >= 0 && k >= 0 && i + 1 < rect.width() && k + 1 < rect.height();
    }
    [[nodiscard]] std::array<const HorizonTexel*, 4> corners(std::int64_t i,
                                                             std::int64_t k) const {
        return {&at(i, k), &at(i + 1, k), &at(i, k + 1), &at(i + 1, k + 1)};
    }
    // Whether the cell (i, k) is cut along its main diagonal, by the heights the grid shows.
    [[nodiscard]] bool mainOf(std::int64_t i, std::int64_t k, CellSplit split,
                              SeaDrawing drawing) const {
        const auto c = corners(i, k);
        const auto h = [&](std::size_t q) { return terrain::unreal::shownHeight(*c[q], drawing); };
        return terrain::unreal::mainDiagonal(split, h(0), h(1), h(2), h(3));
    }
    // The ground the grid shows at (x, z) (no drop, no curvature), on the triangles of its cell.
    [[nodiscard]] std::optional<double> heightAt(double x, double z, CellSplit split,
                                                 SeaDrawing drawing) const {
        std::int64_t i = 0, k = 0;
        double u = 0.0, v = 0.0;
        if (!cellOf(x, z, i, k, u, v)) {
            return std::nullopt;
        }
        const auto w = terrain::unreal::cellWeights(mainOf(i, k, split, drawing), u, v);
        const auto c = corners(i, k);
        double h = 0.0;
        for (std::size_t q = 0; q < 4; ++q) {
            h += w[q] * terrain::unreal::shownHeight(*c[q], drawing);
        }
        return h;
    }
    // The slopes of the grid at (x, z): linear on the triangles of its cell (the vertex normals as
    // the GPU interpolates them), or bilinear over the cell (coast_render_support.hpp, Level0).
    [[nodiscard]] std::optional<std::array<double, 2>> slopeAt(double x, double z, CellSplit split,
                                                               bool bilinear) const {
        std::int64_t i = 0, k = 0;
        double u = 0.0, v = 0.0;
        if (!cellOf(x, z, i, k, u, v)) {
            return std::nullopt;
        }
        const std::array<double, 4> w =
            bilinear ? std::array<double, 4>{(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v}
                     : terrain::unreal::cellWeights(mainOf(i, k, split, SeaDrawing::InGround), u,
                                                    v);
        const auto c = corners(i, k);
        std::array<double, 2> g{};
        for (std::size_t q = 0; q < 4; ++q) {
            g[0] += w[q] * c[q]->gradX;
            g[1] += w[q] * c[q]->gradZ;
        }
        return g;
    }
};

// The samples of the default world with its planet over a rectangle of a level, raised to their
// crests from level 1 on as the GPU horizon raises them (HorizonDetailSampler: their point heights
// and lifts, crestHeight; their point heights where raise is false), as texels, with the detail
// slopes of the level (none without normals).
inline DrawnLevel drawnLevel(int level, const HorizonRect& rect, bool withNormals = true,
                             bool raise = true) {
    static const terrain::Ecology ecology(terrain::kDefaultWorldSeed);
    static const terrain::HorizonSampler sampler(coast_planet::field(), ecology, params());
    static const terrain::HorizonParams crestParams = [] {
        terrain::HorizonParams p = params();
        p.crestRatio = kGpuCrestRatio;
        return p;
    }();
    static const terrain::HorizonDetailSampler details(coast_planet::field(), crestParams,
                                                       kGpuNormalRatio);
    static const terrain::HorizonDetailSampler lifts(coast_planet::field(), crestParams, 0);
    DrawnLevel out;
    out.level = level;
    out.pitch = terrain::levelPitch(params(), level);
    out.rect = rect;
    const std::vector<terrain::HorizonCell> cells = sampler.sample(level, rect);
    terrain::HorizonDetail detail = (withNormals ? details : lifts).sample(level, rect);
    const bool raises = !detail.lifts.empty();
    out.texels.reserve(cells.size());
    for (std::size_t n = 0; n < cells.size(); ++n) {
        terrain::HorizonCell cell = cells[n];
        const float point = raises ? detail.points[n] : cell.height;
        const float raised = raises ? point + detail.lifts[n] : point;
        const std::optional<float> height = terrain::unreal::crestHeight(cell, point, raised);
        if (raise && height) {
            cell.height = *height;
        }
        out.texels.push_back(terrain::unreal::horizonTexel(cell, true));
    }
    out.detail = std::move(detail.slopes);
    return out;
}

// The rectangle of samples of a level of half samples around (x, z).
inline HorizonRect rectAround(int level, double x, double z, std::int64_t half) {
    const double pitch = terrain::levelPitch(params(), level);
    const auto i = static_cast<std::int64_t>(std::floor(x / pitch));
    const auto k = static_cast<std::int64_t>(std::floor(z / pitch));
    return {i - half, k - half, i + half + 1, k + half + 1};
}

} // namespace water_horizon
