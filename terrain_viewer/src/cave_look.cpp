#include "cave_look.hpp"

#include "altitude_tint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace viewer {

namespace {

// The outer surface, as the shader of the streamed world tints it.
constexpr Vec3 kLowlandColor{0.30f, 0.55f, 0.25f};
constexpr Vec3 kHighlandColor{0.65f, 0.60f, 0.55f};

// A speleothem is a cone of kSpeleothemSides sides. Its base is sunk kSpeleothemSunk meters in
// the rock so that no gap shows on uneven walls; its base ring is darker than its tip.
constexpr int kSpeleothemSides = 7;
constexpr float kSpeleothemSunk = 0.3f;
constexpr float kBaseShade = 0.8f;

// Where a speleothem is: the center of its base ring and its tip.
struct ConeFrame {
    Vec3 base;       // center of the ring
    Vec3 tip;
    float radius;    // of the ring
    float direction; // +1 when the tip is above the base, -1 under it
    float reach;     // from the ring to the tip, meters
};

ConeFrame coneFrame(const terrain::Speleothem& s) {
    const float direction = s.kind == terrain::SpeleothemKind::Stalagmite ? 1.0f : -1.0f;
    const auto x = static_cast<float>(s.x);
    const auto z = static_cast<float>(s.z);
    const float reach = s.length + kSpeleothemSunk;
    // The ring is wider than the speleothem so that it has its radius where the rock is.
    return {{x, s.y - direction * kSpeleothemSunk, z},
            {x, s.y + direction * s.length, z},
            s.radius * reach / s.length,
            direction,
            reach};
}

// Normal of the side of the cone at `angle`: outward, leaning toward the tip.
terrain::Vec3f sideNormal(const ConeFrame& cone, float angle) {
    const Vec3 n = normalize({std::cos(angle) * cone.reach, cone.direction * cone.radius,
                              std::sin(angle) * cone.reach});
    return {n.x, n.y, n.z};
}

// Appends the vertices of one speleothem: a ring of kSpeleothemSides at the base, then a tip for
// each side (the normals of the sides change at the tips), and the triangles of the sides.
void appendSpeleothem(SpeleothemMesh& out, const terrain::Speleothem& s) {
    if (s.length <= 0.0f || s.radius <= 0.0f) {
        return;
    }
    const ConeFrame cone = coneFrame(s);
    const Vec3 tipColor =
        caveMaterialColor(s.ice ? terrain::CaveMaterial::Ice : terrain::CaveMaterial::Limestone);
    const Vec3 baseColor = tipColor * kBaseShade;
    const auto first = static_cast<std::uint32_t>(out.mesh.vertices.size());
    constexpr auto sides = static_cast<std::uint32_t>(kSpeleothemSides);
    constexpr float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSpeleothemSides);
    for (std::uint32_t i = 0; i < sides; ++i) {
        const float angle = static_cast<float>(i) * step;
        out.mesh.vertices.push_back({cone.base.x + std::cos(angle) * cone.radius, cone.base.y,
                                     cone.base.z + std::sin(angle) * cone.radius});
        out.mesh.normals.push_back(sideNormal(cone, angle));
        out.colors.push_back(baseColor);
    }
    for (std::uint32_t i = 0; i < sides; ++i) {
        out.mesh.vertices.push_back({cone.tip.x, cone.tip.y, cone.tip.z});
        out.mesh.normals.push_back(sideNormal(cone, (static_cast<float>(i) + 0.5f) * step));
        out.colors.push_back(tipColor);
    }
    // Counter-clockwise seen from outside: the ring turns from x toward z.
    for (std::uint32_t i = 0; i < sides; ++i) {
        const std::uint32_t a = first + i;
        const std::uint32_t b = first + (i + 1) % sides;
        const std::uint32_t tip = first + sides + i;
        const auto triangle = cone.direction > 0.0f ? std::array{b, a, tip} : std::array{a, b, tip};
        out.mesh.indices.insert(out.mesh.indices.end(), triangle.begin(), triangle.end());
    }
}

} // namespace

Vec3 caveMaterialColor(terrain::CaveMaterial material) {
    switch (material) {
    case terrain::CaveMaterial::Surface:
        return kLowlandColor;
    case terrain::CaveMaterial::Stone:
        return {0.46f, 0.46f, 0.48f}; // gray
    case terrain::CaveMaterial::Deepstone:
        return {0.24f, 0.27f, 0.35f}; // dark blue-gray
    case terrain::CaveMaterial::Limestone:
        return {0.82f, 0.75f, 0.58f}; // beige
    case terrain::CaveMaterial::Sandstone:
        return {0.76f, 0.54f, 0.26f}; // ochre
    case terrain::CaveMaterial::Ice:
        return {0.68f, 0.88f, 0.96f}; // pale cyan
    case terrain::CaveMaterial::Moss:
        return {0.24f, 0.42f, 0.17f}; // green
    case terrain::CaveMaterial::Mud:
        return {0.36f, 0.25f, 0.16f}; // brown
    case terrain::CaveMaterial::Gravel:
        return {0.72f, 0.71f, 0.68f}; // light gray
    case terrain::CaveMaterial::Water:
        return {0.10f, 0.36f, 0.62f}; // blue
    case terrain::CaveMaterial::Basalt:
        return {0.17f, 0.15f, 0.18f}; // near black
    case terrain::CaveMaterial::Magma:
        return {0.62f, 0.15f, 0.05f}; // dark glowing red
    }
    return {1.0f, 0.0f, 1.0f};
}

Vec3 altitudeColor(float y) {
    const float t = std::clamp((y - kStreamedLowAltitude) /
                                   (kStreamedHighAltitude - kStreamedLowAltitude),
                               0.0f, 1.0f);
    return kLowlandColor + (kHighlandColor - kLowlandColor) * t;
}

std::vector<Vec3> rockColors(const terrain::TileData& data) {
    const std::size_t count = data.mesh.vertices.size();
    const bool painted = data.materials.size() == count;
    std::vector<Vec3> colors;
    colors.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const terrain::CaveMaterial material =
            painted ? data.materials[i] : terrain::CaveMaterial::Surface;
        if (material == terrain::CaveMaterial::Surface) {
            colors.push_back(
                altitudeColor(static_cast<float>(data.originY + data.mesh.vertices[i].y)));
        } else {
            colors.push_back(caveMaterialColor(material));
        }
    }
    return colors;
}

std::vector<Vec3> lakeColors(const terrain::TileData& data) {
    const std::size_t count = data.water.vertices.size();
    const bool painted = data.waterMaterials.size() == count;
    std::vector<Vec3> colors;
    colors.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const bool frozen = painted && data.waterMaterials[i] == terrain::CaveMaterial::Ice;
        colors.push_back(
            caveMaterialColor(frozen ? terrain::CaveMaterial::Ice : terrain::CaveMaterial::Water));
    }
    return colors;
}

SpeleothemMesh speleothemMesh(const std::vector<terrain::Speleothem>& speleothems) {
    SpeleothemMesh result;
    for (const terrain::Speleothem& speleothem : speleothems) {
        appendSpeleothem(result, speleothem);
    }
    return result;
}

} // namespace viewer
