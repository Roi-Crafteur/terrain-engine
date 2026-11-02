#include "terrain/io/mesh_view.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/io/mesh_checks.hpp"
#include "terrain/io/relief_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace terrain {

namespace {

using Rgb = std::array<double, 3>;

constexpr Rgb kHaze = {200.0, 215.0, 235.0};
constexpr Rgb kWater = {55.0, 105.0, 165.0};
constexpr Rgb kRock = {128.0, 118.0, 108.0};
// The colors of the marks: an opening, an inverted triangle, a triangle seen from behind.
constexpr std::uint8_t kMarkBack = 3;
constexpr std::array<Rgb, 4> kMarkColors = {
    {{0.0, 0.0, 0.0}, {245.0, 215.0, 0.0}, {225.0, 25.0, 25.0}, {0.0, 205.0, 225.0}}};
// The ground turns to bare rock as the tangent of its slope passes kRockLow to kRockHigh (35 to
// 45 degrees), as the views of the relief paint it.
constexpr double kRockLow = 0.7;
constexpr double kRockHigh = 1.0;
// The share of rock of a vertex without one: from the slope of each pixel.
constexpr double kFromSlope = -1.0;
// Triangles are cut at kNear meters before the eye, along the axis of the view.
constexpr double kNear = 0.25;

// A vertex on the image: its position in pixels (pixel (i, j) covers [i, i + 1) x [j, j + 1)), its
// key (larger is nearer: the inverse of its depth in perspective, its height from above), its
// weight (the inverse of its depth in perspective, 1 from above) and what its pixels interpolate.
struct ScreenVertex {
    float x = 0.0f;
    float y = 0.0f;
    float key = 0.0f;
    float w = 1.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float height = 0.0f;
    float rock = 0.0f;
    float distance = 0.0f; // horizontal, meters: the haze
};

// The triangles of one mesh on the image.
struct Projected {
    std::vector<ScreenVertex> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint8_t> marks; // per triangle
    bool water = false;
};

// A point of a mesh in the world, and what its vertex carries.
struct WorldVertex {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double nx = 0.0;
    double ny = 1.0;
    double nz = 0.0;
    double rock = 0.0;
};

WorldVertex lerp(const WorldVertex& a, const WorldVertex& b, double t) {
    return {a.x + (b.x - a.x) * t,   a.y + (b.y - a.y) * t,   a.z + (b.z - a.z) * t,
            a.nx + (b.nx - a.nx) * t, a.ny + (b.ny - a.ny) * t, a.nz + (b.nz - a.nz) * t,
            a.rock + (b.rock - a.rock) * t};
}

// The share of rock of ground whose normal rises ny (unit normal): its slope.
double rockOfSlope(double ny) {
    if (!(ny > 0.0)) {
        return 1.0;
    }
    const double tangent = std::sqrt(std::max(0.0, 1.0 - ny * ny)) / ny;
    return std::clamp((tangent - kRockLow) / (kRockHigh - kRockLow), 0.0, 1.0);
}

WorldVertex worldVertex(const ViewMesh& m, std::size_t i) {
    const Vec3f& p = m.mesh->vertices[i];
    const Vec3f& n = m.mesh->normals[i];
    WorldVertex v{m.originX + p.x, m.originY + p.y, m.originZ + p.z, n.x, n.y, n.z, 0.0};
    // Without shares, the slope decides at each pixel (kFromSlope), as the material of a client
    // does from its interpolated normal.
    v.rock = m.rock.empty() ? kFromSlope : static_cast<double>(m.rock[i]);
    return v;
}

// The camera of a perspective view (relief_view.cpp's): its axis, its right, the focal length in
// pixels, the row of the horizontal and the sun.
struct Camera {
    double forwardX = 0.0;
    double forwardZ = 0.0;
    double rightX = 0.0;
    double rightZ = 0.0;
    double focal = 1.0;
    double centerRow = 0.0;
    std::array<double, 3> sun{};
};

Camera cameraOf(const ViewParams& p, const MeshLight& light) {
    Camera c;
    c.forwardX = dmath::sinTurns(p.heading);
    c.forwardZ = -dmath::cosTurns(p.heading);
    c.rightX = dmath::cosTurns(p.heading);
    c.rightZ = dmath::sinTurns(p.heading);
    const double half = 0.5 * p.fieldOfView;
    c.focal = 0.5 * p.width * dmath::cosTurns(half) / dmath::sinTurns(half);
    c.centerRow = 0.5 * p.height + c.focal * p.tilt;
    const double cosine = std::sqrt(1.0 - light.sunSine * light.sunSine);
    const double azimuth = p.heading - light.sunLeft;
    c.sun = {dmath::sinTurns(azimuth) * cosine, light.sunSine,
             -dmath::cosTurns(azimuth) * cosine};
    return c;
}

double depthOf(const ViewParams& p, const Camera& c, const WorldVertex& v) {
    return (v.x - p.eyeX) * c.forwardX + (v.z - p.eyeZ) * c.forwardZ;
}

ScreenVertex projectPerspective(const ViewParams& p, const Camera& c, const WorldVertex& v) {
    const double dx = v.x - p.eyeX;
    const double dz = v.z - p.eyeZ;
    const double s = dx * c.forwardX + dz * c.forwardZ;
    const double lateral = dx * c.rightX + dz * c.rightZ;
    const double distance = dmath::hypot(dx, dz);
    const double rise = v.y - p.eyeHeight - distance * distance / (2.0 * p.planetRadius);
    ScreenVertex out;
    out.x = static_cast<float>(0.5 * p.width + c.focal * lateral / s);
    out.y = static_cast<float>(c.centerRow - c.focal * rise / s);
    out.w = static_cast<float>(1.0 / s);
    out.key = out.w;
    out.nx = static_cast<float>(v.nx);
    out.ny = static_cast<float>(v.ny);
    out.nz = static_cast<float>(v.nz);
    out.height = static_cast<float>(v.y);
    out.rock = static_cast<float>(v.rock);
    out.distance = static_cast<float>(distance);
    return out;
}

// Appends the triangle (a, b, c) of the world, cut at the near plane, to out.
void addClipped(const ViewParams& p, const Camera& c, const std::array<WorldVertex, 3>& t,
                std::uint8_t mark, Projected& out) {
    std::array<WorldVertex, 4> kept{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const WorldVertex& a = t[i];
        const WorldVertex& b = t[(i + 1) % 3];
        const double da = depthOf(p, c, a) - kNear;
        const double db = depthOf(p, c, b) - kNear;
        if (da >= 0.0) {
            kept[count++] = a;
        }
        if ((da >= 0.0) != (db >= 0.0)) {
            kept[count++] = lerp(a, b, da / (da - db));
        }
    }
    if (count < 3) {
        return;
    }
    const auto first = static_cast<std::uint32_t>(out.vertices.size());
    for (std::size_t i = 0; i < count; ++i) {
        out.vertices.push_back(projectPerspective(p, c, kept[i]));
    }
    for (std::uint32_t i = 2; i < count; ++i) {
        out.triangles.push_back({first, first + i - 1, first + i});
        out.marks.push_back(mark);
    }
}

void validateMesh(const ViewMesh& m) {
    if (m.mesh == nullptr || m.mesh->normals.size() != m.mesh->vertices.size() ||
        m.mesh->indices.size() % 3 != 0 ||
        (!m.rock.empty() && m.rock.size() != m.mesh->vertices.size()) ||
        (!m.marks.empty() && m.marks.size() != m.mesh->indices.size() / 3)) {
        throw std::invalid_argument(
            "ViewMesh: a mesh, its normals, its rock shares and its marks expected");
    }
    for (const std::uint32_t index : m.mesh->indices) {
        if (index >= m.mesh->vertices.size()) {
            throw std::invalid_argument("ViewMesh: an index out of range");
        }
    }
}

// The mark of triangle t of a mesh: its own, else kMarkBack when backFaces asks for it and the
// eye sees it against its winding (toward: from a vertex to the eye).
template <typename Toward>
std::uint8_t markOf(const ViewMesh& m, std::size_t t, bool backFaces, const Toward& toward) {
    const std::uint8_t mark = m.marks.empty() ? kMarkSound : m.marks[t];
    if (mark != kMarkSound || !backFaces) {
        return mark;
    }
    const Vec3f& a = m.mesh->vertices[m.mesh->indices[3 * t]];
    const Vec3f& b = m.mesh->vertices[m.mesh->indices[3 * t + 1]];
    const Vec3f& c = m.mesh->vertices[m.mesh->indices[3 * t + 2]];
    const double ux = static_cast<double>(b.x) - a.x;
    const double uy = static_cast<double>(b.y) - a.y;
    const double uz = static_cast<double>(b.z) - a.z;
    const double vx = static_cast<double>(c.x) - a.x;
    const double vy = static_cast<double>(c.y) - a.y;
    const double vz = static_cast<double>(c.z) - a.z;
    const std::array<double, 3> eye = toward(a);
    const double facing = (uy * vz - uz * vy) * eye[0] + (uz * vx - ux * vz) * eye[1] +
                          (ux * vy - uy * vx) * eye[2];
    return facing < 0.0 ? kMarkBack : kMarkSound;
}

Projected projectMesh(const ViewMesh& m, const ViewParams& p, const Camera& c, bool backFaces) {
    Projected out;
    out.water = m.water;
    const std::size_t count = m.mesh->vertices.size();
    std::vector<bool> front(count);
    out.vertices.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const WorldVertex v = worldVertex(m, i);
        front[i] = depthOf(p, c, v) >= kNear;
        out.vertices.push_back(front[i] ? projectPerspective(p, c, v) : ScreenVertex{});
    }
    const std::vector<std::uint32_t>& indices = m.mesh->indices;
    const auto toward = [&](const Vec3f& a) {
        return std::array<double, 3>{p.eyeX - m.originX - a.x, p.eyeHeight - m.originY - a.y,
                                     p.eyeZ - m.originZ - a.z};
    };
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const std::array<std::uint32_t, 3> tri{indices[t], indices[t + 1], indices[t + 2]};
        const int inFront = static_cast<int>(front[tri[0]]) + static_cast<int>(front[tri[1]]) +
                            static_cast<int>(front[tri[2]]);
        if (inFront == 0) {
            continue;
        }
        const std::uint8_t mark = markOf(m, t / 3, backFaces, toward);
        if (inFront == 3) {
            out.triangles.push_back(tri);
            out.marks.push_back(mark);
        } else {
            addClipped(p, c, {worldVertex(m, tri[0]), worldVertex(m, tri[1]),
                              worldVertex(m, tri[2])},
                       mark, out);
        }
    }
    return out;
}

Projected projectTop(const ViewMesh& m, const TopViewParams& p) {
    Projected out;
    out.water = m.water;
    const double x0 = p.centerX - 0.5 * p.width * p.metersPerPixel;
    const double z0 = p.centerZ - 0.5 * p.height * p.metersPerPixel;
    for (std::size_t i = 0; i < m.mesh->vertices.size(); ++i) {
        const WorldVertex v = worldVertex(m, i);
        ScreenVertex s;
        s.x = static_cast<float>((v.x - x0) / p.metersPerPixel);
        s.y = static_cast<float>((v.z - z0) / p.metersPerPixel);
        s.key = static_cast<float>(v.y);
        s.nx = static_cast<float>(v.nx);
        s.ny = static_cast<float>(v.ny);
        s.nz = static_cast<float>(v.nz);
        s.height = s.key;
        s.rock = static_cast<float>(v.rock);
        out.vertices.push_back(s);
    }
    for (std::size_t t = 0; t + 2 < m.mesh->indices.size(); t += 3) {
        out.triangles.push_back(
            {m.mesh->indices[t], m.mesh->indices[t + 1], m.mesh->indices[t + 2]});
        out.marks.push_back(m.marks.empty() ? kMarkSound : m.marks[t / 3]);
    }
    return out;
}

// What the pixels of a view are drawn with.
struct Canvas {
    Image& image;
    std::vector<float>& keys; // per pixel, larger is nearer
    std::array<double, 3> sun{};
    double hazeDistance = 0.0; // 0: no haze
};

std::uint8_t toByte(double value) {
    return static_cast<std::uint8_t>(std::clamp(value + 0.5, 0.0, 255.0));
}

Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// The color of a pixel of a mesh from what it interpolates.
Rgb shade(const Canvas& canvas, const ScreenVertex& v, bool water, std::uint8_t mark) {
    if (mark != kMarkSound) {
        return kMarkColors[mark];
    }
    const double length = std::max(1e-12, std::sqrt(static_cast<double>(v.nx) * v.nx +
                                                    static_cast<double>(v.ny) * v.ny +
                                                    static_cast<double>(v.nz) * v.nz));
    const double lambert = (v.nx * canvas.sun[0] + v.ny * canvas.sun[1] + v.nz * canvas.sun[2]) /
                           length;
    const double light = std::clamp(0.45 + 0.75 * lambert, 0.3, 1.2);
    Rgb color = kWater;
    if (!water) {
        const std::array<float, 3> tint = reliefColor(v.height, false);
        const double rock = v.rock < 0.0f ? rockOfSlope(v.ny / length) : v.rock;
        color = mix({tint[0], tint[1], tint[2]}, kRock, std::clamp(rock, 0.0, 1.0));
        color = {color[0] * light, color[1] * light, color[2] * light};
    }
    if (canvas.hazeDistance > 0.0) {
        color = mix(color, kHaze, 1.0 - dmath::exp(-v.distance / canvas.hazeDistance));
    }
    return color;
}

double edge(const ScreenVertex& a, const ScreenVertex& b, double x, double y) {
    return (static_cast<double>(b.x) - a.x) * (y - a.y) -
           (static_cast<double>(b.y) - a.y) * (x - a.x);
}

// The vertex at the barycentric weights (l0, l1, l2) of a triangle, perspective-correct.
ScreenVertex interpolate(const std::array<const ScreenVertex*, 3>& t, double l0, double l1,
                         double l2) {
    const double w = l0 * t[0]->w + l1 * t[1]->w + l2 * t[2]->w;
    const double k0 = l0 * t[0]->w / w;
    const double k1 = l1 * t[1]->w / w;
    const double k2 = l2 * t[2]->w / w;
    const auto at = [&](float ScreenVertex::*field) {
        return static_cast<float>(k0 * (t[0]->*field) + k1 * (t[1]->*field) +
                                  k2 * (t[2]->*field));
    };
    ScreenVertex v;
    v.key = static_cast<float>(l0 * t[0]->key + l1 * t[1]->key + l2 * t[2]->key);
    v.nx = at(&ScreenVertex::nx);
    v.ny = at(&ScreenVertex::ny);
    v.nz = at(&ScreenVertex::nz);
    v.height = at(&ScreenVertex::height);
    v.rock = at(&ScreenVertex::rock);
    v.distance = at(&ScreenVertex::distance);
    return v;
}

// Draws a triangle within rows [first, last) of the canvas.
void drawTriangle(const std::array<const ScreenVertex*, 3>& t, bool water, std::uint8_t mark,
                  int first, int last, Canvas& canvas) {
    const double area = edge(*t[0], *t[1], t[2]->x, t[2]->y);
    if (!(std::abs(area) > 0.0)) {
        return;
    }
    const auto [lowY, highY] = std::minmax({t[0]->y, t[1]->y, t[2]->y});
    const auto [lowX, highX] = std::minmax({t[0]->x, t[1]->x, t[2]->x});
    const int width = canvas.image.width;
    const int top = std::max(first, static_cast<int>(std::floor(lowY - 0.5f)));
    const int bottom = std::min(last - 1, static_cast<int>(std::ceil(highY - 0.5f)));
    const int left = std::max(0, static_cast<int>(std::floor(lowX - 0.5f)));
    const int right = std::min(width - 1, static_cast<int>(std::ceil(highX - 0.5f)));
    for (int row = top; row <= bottom; ++row) {
        const double y = row + 0.5;
        for (int column = left; column <= right; ++column) {
            const double x = column + 0.5;
            const double l0 = edge(*t[1], *t[2], x, y) / area;
            const double l1 = edge(*t[2], *t[0], x, y) / area;
            const double l2 = 1.0 - l0 - l1;
            if (l0 < 0.0 || l1 < 0.0 || l2 < 0.0) {
                continue;
            }
            const ScreenVertex v = interpolate(t, l0, l1, l2);
            const auto at = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(column);
            if (!(v.key > canvas.keys[at])) {
                continue;
            }
            canvas.keys[at] = v.key;
            const Rgb color = shade(canvas, v, water, mark);
            for (std::size_t k = 0; k < 3; ++k) {
                canvas.image.pixels[at * 3 + k] = toByte(color[k]);
            }
        }
    }
}

void drawAll(const std::vector<Projected>& meshes, int threads, Canvas& canvas) {
    planet::parallelRows(canvas.image.height, threads, [&](int first, int last) {
        for (const Projected& m : meshes) {
            for (std::size_t t = 0; t < m.triangles.size(); ++t) {
                const auto& tri = m.triangles[t];
                drawTriangle({&m.vertices[tri[0]], &m.vertices[tri[1]], &m.vertices[tri[2]]},
                             m.water, m.marks[t], first, last, canvas);
            }
        }
    });
}

template <typename Project>
std::vector<Projected> projectAll(std::span<const ViewMesh> meshes, int threads,
                                  const Project& project) {
    for (const ViewMesh& m : meshes) {
        validateMesh(m);
    }
    std::vector<Projected> out(meshes.size());
    planet::parallelBlocks(static_cast<int>(meshes.size()), 1, threads, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            out[static_cast<std::size_t>(i)] = project(meshes[static_cast<std::size_t>(i)]);
        }
    });
    return out;
}

} // namespace

Image renderMeshView(std::span<const ViewMesh> meshes, const Image& background,
                     const ViewParams& params, int threads, const MeshLight& light) {
    if (params.width < 2 || params.height < 2 || !(params.fieldOfView > 0.0) ||
        !(params.fieldOfView < 0.45) || !(params.planetRadius > 0.0) ||
        !(params.hazeDistance > 0.0) || !std::isfinite(params.eyeHeight) ||
        !std::isfinite(params.tilt) || !(light.sunSine > 0.0) || !(light.sunSine < 1.0)) {
        throw std::invalid_argument("renderMeshView: invalid image, field of view, distances or sun");
    }
    if (background.width != params.width || background.height != params.height ||
        background.channels != 3) {
        throw std::invalid_argument("renderMeshView: a background of the size of the view");
    }
    const int workers = planet::resolveThreads(threads);
    const Camera camera = cameraOf(params, light);
    const std::vector<Projected> projected = projectAll(
        meshes, workers,
        [&](const ViewMesh& m) { return projectMesh(m, params, camera, light.backFaces); });
    Image image = background;
    std::vector<float> keys(static_cast<std::size_t>(params.width) *
                                static_cast<std::size_t>(params.height),
                            0.0f);
    Canvas canvas{image, keys, camera.sun, params.hazeDistance};
    drawAll(projected, workers, canvas);
    return image;
}

Image renderTopView(std::span<const ViewMesh> meshes, const TopViewParams& params, int threads) {
    if (params.width < 2 || params.height < 2 || !std::isfinite(params.metersPerPixel) ||
        !(params.metersPerPixel > 0.0) || !(params.sunSine > 0.0) || !(params.sunSine < 1.0)) {
        throw std::invalid_argument("renderTopView: invalid image, scale or sun");
    }
    const int workers = planet::resolveThreads(threads);
    const std::vector<Projected> projected =
        projectAll(meshes, workers, [&](const ViewMesh& m) { return projectTop(m, params); });
    const auto pixels =
        static_cast<std::size_t>(params.width) * static_cast<std::size_t>(params.height);
    Image image{params.width, params.height, 3, std::vector<std::uint8_t>(pixels * 3, 0)};
    std::vector<float> keys(pixels, -std::numeric_limits<float>::infinity());
    const double cosine = std::sqrt(1.0 - params.sunSine * params.sunSine);
    // From above, -Z (the north) is up the image: the sun of azimuth a lies along (sin a, -cos a).
    const std::array<double, 3> sun{dmath::sinTurns(params.sunAzimuth) * cosine, params.sunSine,
                                    -dmath::cosTurns(params.sunAzimuth) * cosine};
    Canvas canvas{image, keys, sun, 0.0};
    drawAll(projected, workers, canvas);
    return image;
}

} // namespace terrain
