#include "tile_mesh_conversion.hpp"

#include <limits>
#include <stdexcept>

namespace terrain::unreal {

namespace {

void validate(const Mesh& mesh) {
    if (mesh.normals.size() != mesh.vertices.size()) {
        throw std::invalid_argument("convertMesh: one normal per vertex expected");
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

UnrealMesh convertMesh(const Mesh& mesh, double originY) {
    validate(mesh);
    UnrealMesh out;
    out.positions.reserve(mesh.vertices.size());
    out.normals.reserve(mesh.normals.size());
    out.colors.reserve(mesh.vertices.size());
    for (const Vec3f& vertex : mesh.vertices) {
        out.positions.push_back(toUnrealOffset(vertex));
        out.colors.push_back(altitudeColor(originY + vertex.y));
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
