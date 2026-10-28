#include "tile_mesh_conversion.hpp"

#include "terrain/generation/water_kind.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

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

} // namespace

VertexWater vertexWater(float level, double altitude) {
    if (!(level > kNoWaterLevel)) {
        return {};
    }
    return {static_cast<float>(static_cast<double>(level) - altitude), 1.0f};
}

UnrealMesh convertMesh(const Mesh& mesh, double originY, std::span<const float> waterLevels,
                       std::span<const float> rock) {
    validate(mesh, waterLevels);
    if (!rock.empty() && rock.size() != mesh.vertices.size()) {
        throw std::invalid_argument("convertMesh: one share of rock per vertex expected");
    }
    UnrealMesh out;
    out.positions.reserve(mesh.vertices.size());
    out.normals.reserve(mesh.normals.size());
    out.colors.reserve(mesh.vertices.size());
    out.water.reserve(mesh.vertices.size());
    for (std::size_t n = 0; n < mesh.vertices.size(); ++n) {
        const Vec3f& vertex = mesh.vertices[n];
        out.positions.push_back(toUnrealOffset(vertex));
        out.colors.push_back(altitudeColor(originY + vertex.y));
        out.water.push_back(waterLevels.empty() ? VertexWater{}
                                                : vertexWater(waterLevels[n], originY + vertex.y));
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
