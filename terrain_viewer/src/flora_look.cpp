#include "flora_look.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace viewer {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// A fern: kFernFronds kites fanned around its axis. A frond leans outward by kFernLean * size at
// its tip, and is widest (kFernWidth * size on each side) kFernWidest of the way up.
constexpr int kFernFronds = 5;
constexpr float kFernLean = 0.45f;
constexpr float kFernWidth = 0.2f;
constexpr float kFernWidest = 0.45f;
constexpr Vec3 kFernBase{0.10f, 0.35f, 0.12f};
constexpr Vec3 kFernTip{0.38f, 0.78f, 0.28f};

// A mushroom: a stem of kMushroomSides sides, radius kStemRadius * size, up to kStemHeight * size,
// under a cap whose rim (radius kCapRadius * size) is at kCapRim * size and whose apex is at the
// top of the box.
constexpr int kMushroomSides = 6;
constexpr float kStemRadius = 0.06f;
constexpr float kStemHeight = 0.6f;
constexpr float kCapRadius = 0.4f;
constexpr float kCapRim = 0.55f;
constexpr Vec3 kStemFoot{0.62f, 0.60f, 0.52f};
constexpr Vec3 kStemTop{0.90f, 0.88f, 0.76f};
constexpr Vec3 kCapEdge{0.22f, 0.66f, 0.66f};
constexpr Vec3 kCapApex{0.60f, 1.00f, 0.90f};

// A vine: two crossed strips kVineHalfWidth meters wide on each side at the ceiling, narrowing to
// kVineTaper of that at the tip.
constexpr float kVineHalfWidth = 0.07f;
constexpr float kVineTaper = 0.4f;
constexpr Vec3 kVineTop{0.12f, 0.30f, 0.10f};
constexpr Vec3 kVineTip{0.42f, 0.72f, 0.22f};

Vec3 vec(const terrain::CaveFlora& plant) {
    return {static_cast<float>(plant.x), plant.y, static_cast<float>(plant.z)};
}

// An angle in [0, 2 pi) that only depends on the position, so that plants do not all face the same
// way.
float spin(const terrain::CaveFlora& plant) {
    const double hash = std::sin(plant.x * 12.9898 + plant.z * 78.233) * 43758.5453;
    return static_cast<float>(hash - std::floor(hash)) * kTwoPi;
}

void pushVertex(PaintedMesh& out, Vec3 position, Vec3 normal, Vec3 color) {
    out.mesh.vertices.push_back({position.x, position.y, position.z});
    out.mesh.normals.push_back({normal.x, normal.y, normal.z});
    out.colors.push_back(color);
}

void pushTriangle(PaintedMesh& out, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    out.mesh.indices.insert(out.mesh.indices.end(), {a, b, c});
}

// A flat quad of corners p[0] to p[3] in order, with one color per corner. Its normal is the one
// of its counter-clockwise side, which the shader flips for the side seen from behind.
void appendQuad(PaintedMesh& out, const std::array<Vec3, 4>& p, const std::array<Vec3, 4>& color) {
    const Vec3 normal = normalize(cross(p[1] - p[0], p[2] - p[0]));
    const auto first = static_cast<std::uint32_t>(out.mesh.vertices.size());
    for (std::size_t k = 0; k < p.size(); ++k) {
        pushVertex(out, p[k], normal, color[k]);
    }
    pushTriangle(out, first, first + 1, first + 2);
    pushTriangle(out, first, first + 2, first + 3);
}

void appendFern(PaintedMesh& out, const terrain::CaveFlora& plant) {
    const Vec3 foot = vec(plant);
    const Vec3 rise{0.0f, plant.size, 0.0f};
    const Vec3 widestColor = lerp(kFernBase, kFernTip, kFernWidest);
    for (int frond = 0; frond < kFernFronds; ++frond) {
        const float angle = spin(plant) + kTwoPi * static_cast<float>(frond) / kFernFronds;
        const Vec3 outward{std::cos(angle), 0.0f, std::sin(angle)};
        const Vec3 side{-outward.z, 0.0f, outward.x};
        const Vec3 tip = foot + rise + outward * (kFernLean * plant.size);
        const Vec3 widest = lerp(foot, tip, kFernWidest);
        const Vec3 half = side * (kFernWidth * plant.size);
        appendQuad(out, {foot, widest + half, tip, widest - half},
                   {kFernBase, widestColor, kFernTip, widestColor});
    }
}

// The stem: two rings of kMushroomSides vertices (foot, then top) and the sides between them.
void appendStem(PaintedMesh& out, const terrain::CaveFlora& plant) {
    constexpr auto sides = static_cast<std::uint32_t>(kMushroomSides);
    const Vec3 foot = vec(plant);
    const auto first = static_cast<std::uint32_t>(out.mesh.vertices.size());
    for (const float height : {0.0f, kStemHeight * plant.size}) {
        for (std::uint32_t i = 0; i < sides; ++i) {
            const float angle = kTwoPi * static_cast<float>(i) / kMushroomSides;
            const Vec3 outward{std::cos(angle), 0.0f, std::sin(angle)};
            const Vec3 position = foot + outward * (kStemRadius * plant.size) + Vec3{0, height, 0};
            pushVertex(out, position, outward, height > 0.0f ? kStemTop : kStemFoot);
        }
    }
    // Counter-clockwise seen from outside: the rings turn from x toward z.
    for (std::uint32_t i = 0; i < sides; ++i) {
        const std::uint32_t next = (i + 1) % sides;
        pushTriangle(out, first + next, first + i, first + sides + i);
        pushTriangle(out, first + next, first + sides + i, first + sides + next);
    }
}

// The cap: a ring of kMushroomSides vertices at the rim (normals leaning toward the apex), the
// apex, and a triangle per side.
void appendCap(PaintedMesh& out, const terrain::CaveFlora& plant) {
    constexpr auto sides = static_cast<std::uint32_t>(kMushroomSides);
    const Vec3 foot = vec(plant);
    const float reach = kCapRadius * plant.size;
    const float rise = (1.0f - kCapRim) * plant.size;
    const auto first = static_cast<std::uint32_t>(out.mesh.vertices.size());
    for (std::uint32_t i = 0; i < sides; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) / kMushroomSides;
        const Vec3 outward{std::cos(angle), 0.0f, std::sin(angle)};
        const Vec3 rim = foot + outward * reach + Vec3{0.0f, kCapRim * plant.size, 0.0f};
        const Vec3 normal = normalize({outward.x * rise, reach, outward.z * rise});
        pushVertex(out, rim, normal, kCapEdge);
    }
    pushVertex(out, foot + Vec3{0.0f, plant.size, 0.0f}, {0.0f, 1.0f, 0.0f}, kCapApex);
    for (std::uint32_t i = 0; i < sides; ++i) {
        pushTriangle(out, first + (i + 1) % sides, first + i, first + sides);
    }
}

void appendVine(PaintedMesh& out, const terrain::CaveFlora& plant) {
    const Vec3 top = vec(plant);
    const Vec3 tip = top - Vec3{0.0f, plant.size, 0.0f};
    for (int strip = 0; strip < 2; ++strip) {
        const float quarterTurn = 0.5f * std::numbers::pi_v<float>;
        const float angle = spin(plant) + quarterTurn * static_cast<float>(strip);
        const Vec3 wide = Vec3{std::cos(angle), 0.0f, std::sin(angle)} * kVineHalfWidth;
        const Vec3 narrow = wide * kVineTaper;
        appendQuad(out, {top - wide, tip - narrow, tip + narrow, top + wide},
                   {kVineTop, kVineTip, kVineTip, kVineTop});
    }
}

} // namespace

PaintedMesh caveFloraMesh(const std::vector<terrain::CaveFlora>& flora) {
    PaintedMesh result;
    for (const terrain::CaveFlora& plant : flora) {
        if (!(plant.size > 0.0f)) {
            continue;
        }
        switch (plant.kind) {
        case terrain::CaveFloraKind::Fern:
            appendFern(result, plant);
            break;
        case terrain::CaveFloraKind::Mushroom:
            appendStem(result, plant);
            appendCap(result, plant);
            break;
        case terrain::CaveFloraKind::Vine:
            appendVine(result, plant);
            break;
        }
    }
    return result;
}

} // namespace viewer
