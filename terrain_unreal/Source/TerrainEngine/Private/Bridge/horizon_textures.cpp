#include "horizon_textures.hpp"

#include "terrain/generation/heightfield.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

namespace {

// The cells of the trim: the square of the body hole outside the finer window.
void addTrimCells(GridMesh& mesh, const CellRect& square, const CellRect& finer) {
    const int width = square.i1 - square.i0 + 1;
    std::vector<std::int32_t> index(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(square.k1 - square.k0 + 1), -1);
    const auto vertexAt = [&](int i, int k) {
        std::int32_t& slot = index[static_cast<std::size_t>((k - square.k0) * width + i - square.i0)];
        if (slot < 0) {
            slot = static_cast<std::int32_t>(mesh.vertices.size());
            mesh.vertices.push_back(
                {static_cast<float>(i), static_cast<float>(k), GridVertexKind::Sample});
        }
        return slot;
    };
    for (int k = square.k0; k < square.k1; ++k) {
        for (int i = square.i0; i < square.i1; ++i) {
            if (finer.contains(i, k)) {
                continue;
            }
            const std::int32_t v00 = vertexAt(i, k);
            const std::int32_t v10 = vertexAt(i + 1, k);
            const std::int32_t v01 = vertexAt(i, k + 1);
            const std::int32_t v11 = vertexAt(i + 1, k + 1);
            // Counter-clockwise from above in the core, as the cells of the bodies.
            mesh.triangles.insert(mesh.triangles.end(), {v00, v01, v10, v10, v01, v11});
        }
    }
}

CellRect finerAt(int cells, int dx, int dz) {
    const int quarter = cells / 4;
    return {quarter + dx, quarter + dz, quarter + dx + cells / 2, quarter + dz + cells / 2};
}

} // namespace

std::vector<SlotSpan> slotSpans(std::int64_t first, std::int64_t last, int side) {
    const int start = static_cast<int>(slotOf({first, 0}, side).x);
    const auto length = static_cast<int>(last - first);
    if (start + length <= side) {
        return {{first, start, length}};
    }
    const int head = side - start;
    return {{first, start, head}, {first + head, 0, length - head}};
}

HorizonTexel horizonTexel(const HorizonCell& cell, bool sea) {
    if (sea && isSeaCell(cell)) {
        return {kSeaLevel, 0.0f, 0.0f, cell.height - kSeaLevel};
    }
    const bool inland = (cell.flags & kHorizonWater) != 0 && (cell.flags & kHorizonSea) == 0;
    return {cell.height, cell.gradX, cell.gradZ, packSurface(cell.material, cell.biome, inland)};
}

bool isSeaTexel(const HorizonTexel& texel) {
    return texel.color < 0.0f;
}

float texelGround(const HorizonTexel& texel) {
    return isSeaTexel(texel) ? texel.color : std::max(texel.height - kSeaLevel, 0.0f);
}

std::vector<float> pieceTexels(const HorizonPiece& piece, bool sea) {
    std::vector<float> data;
    data.reserve(piece.cells.size() * kTexelFloats);
    for (const HorizonCell& cell : piece.cells) {
        const HorizonTexel texel = horizonTexel(cell, sea);
        data.insert(data.end(), {texel.height, texel.gradX, texel.gradZ, texel.color});
    }
    return data;
}

std::vector<TexelRegion> texelRegions(int level, int side, const HorizonRect& rect,
                                      std::span<const float> data) {
    if (rect.width() > side || rect.height() > side ||
        data.size() != static_cast<std::size_t>(rect.count()) * kTexelFloats) {
        throw std::invalid_argument("texelRegions: the texels do not fit the rectangle");
    }
    std::vector<TexelRegion> regions;
    for (const SlotSpan& z : slotSpans(rect.z0, rect.z1, side)) {
        for (const SlotSpan& x : slotSpans(rect.x0, rect.x1, side)) {
            TexelRegion region{level, x.slot, z.slot, x.length, z.length, {}};
            region.data.reserve(static_cast<std::size_t>(x.length * z.length * kTexelFloats));
            for (int row = 0; row < z.length; ++row) {
                const std::int64_t first =
                    ((z.sample - rect.z0 + row) * rect.width() + (x.sample - rect.x0)) * kTexelFloats;
                const auto begin = data.begin() + static_cast<std::ptrdiff_t>(first);
                region.data.insert(region.data.end(), begin, begin + x.length * kTexelFloats);
            }
            regions.push_back(std::move(region));
        }
    }
    return regions;
}

GridMesh bodyGrid(const BodyTopology& topology, double seaHalfCells) {
    GridMesh mesh;
    const int side = topology.cells + 1;
    mesh.vertices.reserve(topology.vertexCount() - 1);
    for (const std::int32_t sample : topology.samples) {
        mesh.vertices.push_back({static_cast<float>(sample % side),
                                 static_cast<float>(sample / side), GridVertexKind::Sample});
    }
    for (const std::int32_t top : topology.border) {
        GridVertex below = mesh.vertices[static_cast<std::size_t>(top)];
        below.kind = GridVertexKind::Skirt;
        mesh.vertices.push_back(below);
    }
    if (topology.seaFirst >= 0) {
        const double center = topology.cells / 2.0;
        const auto low = static_cast<float>(center - seaHalfCells);
        const auto high = static_cast<float>(center + seaHalfCells);
        for (const GridVertex corner : {GridVertex{low, low, GridVertexKind::Sea},
                                        GridVertex{high, low, GridVertexKind::Sea},
                                        GridVertex{low, high, GridVertexKind::Sea},
                                        GridVertex{high, high, GridVertexKind::Sea}}) {
            mesh.vertices.push_back(corner);
        }
    }
    mesh.triangles = topology.triangles; // they never use the bounds vertex, the last one
    return mesh;
}

GridMesh trimGrid(int cells, int dx, int dz) {
    if (std::max(std::abs(dx), std::abs(dz)) > 1 || cells < 8 || cells % 4 != 0) {
        throw std::invalid_argument("trimGrid: a window within one cell of the center expected");
    }
    GridMesh mesh;
    addTrimCells(mesh, centeredSquare(cells, cells / 4 + 1), finerAt(cells, dx, dz));
    return mesh;
}

int trimVariant(const CellRect& finer, int cells) {
    const int dx = finer.i0 - cells / 4;
    const int dz = finer.k0 - cells / 4;
    if (std::max(std::abs(dx), std::abs(dz)) > 1 || !(finerAt(cells, dx, dz) == finer)) {
        throw std::invalid_argument("trimVariant: the finer window is off center");
    }
    return (dx + 1) + 3 * (dz + 1);
}

std::uint16_t waterCode(float level) {
    if (!(level > kNoWaterLevel)) {
        return 0;
    }
    const double clamped = std::clamp(static_cast<double>(level), double{kWaterLevelLowest},
                                      double{kWaterLevelHighest});
    const double steps = std::floor((clamped - kWaterLevelLowest) / kWaterLevelStep + 0.5);
    return static_cast<std::uint16_t>(1 + static_cast<int>(steps));
}

float waterCodeLevel(std::uint16_t code) {
    return code == 0 ? kNoWaterLevel
                     : kWaterLevelLowest + static_cast<float>(code - 1) * kWaterLevelStep;
}

std::vector<float> pieceUploads(const HorizonPiece& piece, bool sea) {
    std::vector<float> data;
    data.reserve(piece.cells.size() * (kTexelFloats + 1));
    for (const HorizonCell& cell : piece.cells) {
        const HorizonTexel texel = horizonTexel(cell, sea);
        data.insert(data.end(), {texel.height, texel.gradX, texel.gradZ, texel.color});
    }
    for (const HorizonCell& cell : piece.cells) {
        data.push_back(static_cast<float>(waterCode(cell.waterLevel)));
    }
    return data;
}

std::vector<WaterRegion> waterRegions(int level, int side, const HorizonRect& rect,
                                      std::span<const float> codes) {
    if (rect.width() > side || rect.height() > side ||
        codes.size() != static_cast<std::size_t>(rect.count())) {
        throw std::invalid_argument("waterRegions: the codes do not fit the rectangle");
    }
    std::vector<WaterRegion> regions;
    for (const SlotSpan& z : slotSpans(rect.z0, rect.z1, side)) {
        for (const SlotSpan& x : slotSpans(rect.x0, rect.x1, side)) {
            WaterRegion region{level, x.slot, z.slot, x.length, z.length, {}};
            region.data.reserve(static_cast<std::size_t>(x.length) *
                                static_cast<std::size_t>(z.length));
            for (int row = 0; row < z.length; ++row) {
                const std::int64_t first =
                    (z.sample - rect.z0 + row) * rect.width() + (x.sample - rect.x0);
                for (int column = 0; column < x.length; ++column) {
                    region.data.push_back(static_cast<std::uint16_t>(
                        codes[static_cast<std::size_t>(first + column)]));
                }
            }
            regions.push_back(std::move(region));
        }
    }
    return regions;
}

} // namespace terrain::unreal
