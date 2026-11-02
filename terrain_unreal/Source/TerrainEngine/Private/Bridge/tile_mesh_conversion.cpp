#include "tile_mesh_conversion.hpp"

#include "terrain/generation/water_kind.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace terrain::unreal {

namespace {

void validate(const Mesh& mesh, std::span<const float> waterLevels) {
    if (mesh.normals.size() != mesh.vertices.size()) {
        throw std::invalid_argument("convertMesh: one normal per vertex expected");
    }
    if (!waterLevels.empty() && waterLevels.size() != mesh.vertices.size()) {
        throw std::invalid_argument("convertMesh: no water level or one per vertex expected");
    }
    if (mesh.indices.size() % 3 != 0) {
        throw std::invalid_argument("convertMesh: three indices per triangle expected");
    }
    if (mesh.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("convertMesh: too many vertices for int32 indices");
    }
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            throw std::invalid_argument("convertMesh: index out of range");
        }
    }
}

double oceanWeight(BlendTexel texel) {
    const SurfaceBlend blend = unpackBlend(texel);
    double weight = 0.0;
    for (std::size_t s = 0; s < blend.biomes.size(); ++s) {
        if (blend.biomes[s] == Biome::Ocean) {
            weight += static_cast<double>(blend.biomeWeights[s]);
        }
    }
    return weight;
}

constexpr int kSide = kTileBlendTexels;

// The weights of the ocean of the texels of a tile, in their order.
std::vector<double> oceanWeights(const TileSea& sea) {
    if (sea.texels == nullptr || sea.texels->size() != static_cast<std::size_t>(kSide * kSide) ||
        !(sea.pitchMeters > 0.0)) {
        throw std::invalid_argument("oceanShare: the texels of a tile and their pitch expected");
    }
    std::vector<double> weights;
    weights.reserve(sea.texels->size());
    for (const BlendTexel texel : *sea.texels) {
        weights.push_back(oceanWeight(texel));
    }
    return weights;
}

double bilinear(const std::vector<double>& weights, double pitch, double x, double z) {
    const double u = std::clamp(x / pitch, 0.0, kSide - 1.0);
    const double v = std::clamp(z / pitch, 0.0, kSide - 1.0);
    const int i = std::min(static_cast<int>(u), kSide - 2);
    const int k = std::min(static_cast<int>(v), kSide - 2);
    const double fu = u - i;
    const double fv = v - k;
    const auto at = [&](int di, int dk) {
        return weights[static_cast<std::size_t>((k + dk) * kSide + i + di)];
    };
    return (1.0 - fv) * ((1.0 - fu) * at(0, 0) + fu * at(1, 0)) +
           fv * ((1.0 - fu) * at(0, 1) + fu * at(1, 1));
}

} // namespace

double oceanShare(const TileSea& sea, double x, double z) {
    return bilinear(oceanWeights(sea), sea.pitchMeters, x, z);
}

VertexWater vertexWater(float level, double altitude) {
    if (!(level > kNoWaterLevel)) {
        return {};
    }
    return {static_cast<float>(static_cast<double>(level) - altitude), 1.0f};
}

UnrealMesh convertMesh(const Mesh& mesh, double originY, std::span<const float> waterLevels,
                       const TileSea& sea, std::span<const float> rock) {
    validate(mesh, waterLevels);
    if (!rock.empty() && rock.size() != mesh.vertices.size()) {
        throw std::invalid_argument("convertMesh: one share of rock per vertex expected");
    }
    const bool flagged = sea.texels != nullptr && !sea.texels->empty();
    const std::vector<double> ocean = flagged ? oceanWeights(sea) : std::vector<double>{};
    const bool anyOcean = std::any_of(ocean.begin(), ocean.end(), [](double w) { return w > 0.0; });
    UnrealMesh out;
    out.positions.reserve(mesh.vertices.size());
    out.normals.reserve(mesh.normals.size());
    out.colors.reserve(mesh.vertices.size());
    out.water.reserve(mesh.vertices.size());
    for (std::size_t n = 0; n < mesh.vertices.size(); ++n) {
        const Vec3f& vertex = mesh.vertices[n];
        out.positions.push_back(toUnrealOffset(vertex));
        const double altitude = originY + vertex.y;
        const bool under =
            !flagged || (anyOcean && bilinear(ocean, sea.pitchMeters, vertex.x, vertex.z) >= 0.5);
        out.colors.push_back(groundColor(altitude, under));
        out.water.push_back(waterLevels.empty() ? VertexWater{}
                                                : vertexWater(waterLevels[n], altitude));
        if (!rock.empty()) {
            const double share = std::clamp(static_cast<double>(rock[n]), 0.0, 1.0);
            out.colors.back().a = static_cast<std::uint8_t>(std::lround(255.0 * (1.0 - share)));
        }
    }
    for (const Vec3f& normal : mesh.normals) {
        out.normals.push_back(toUnrealDirection(normal));
    }
    out.triangles.reserve(mesh.indices.size());
    for (const std::uint32_t index : mesh.indices) {
        out.triangles.push_back(static_cast<std::int32_t>(index));
    }
    return out;
}

UnrealPoint tileOrigin(const UnrealFrame& frame, const TileData& data, const TileKey& image) {
    const ChunkCoord canonical = data.request.key.origin;
    const auto chunks = [](std::int32_t to, std::int32_t from) {
        return static_cast<double>(std::int64_t{to} - std::int64_t{from});
    };
    const double shiftX = chunks(image.origin.x, canonical.x) * kChunkSizeMeters;
    const double shiftZ = chunks(image.origin.z, canonical.z) * kChunkSizeMeters;
    return toUnreal(frame, {data.originX + shiftX, data.originY, data.originZ + shiftZ});
}

} // namespace terrain::unreal
