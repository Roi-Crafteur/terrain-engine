#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace viewer {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

constexpr float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0f; }

// Column-major 4x4 matrix, laid out as OpenGL expects: (row, col) is m[col * 4 + row].
struct Mat4 {
    std::array<float, 16> m{};

    static constexpr Mat4 identity() {
        Mat4 result;
        for (int i = 0; i < 4; ++i) {
            result.at(i, i) = 1.0f;
        }
        return result;
    }

    constexpr float& at(int row, int col) { return m[static_cast<std::size_t>(col * 4 + row)]; }
    constexpr float at(int row, int col) const { return m[static_cast<std::size_t>(col * 4 + row)]; }
};

constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.at(row, k) * b.at(k, col);
            }
            result.at(row, col) = sum;
        }
    }
    return result;
}

// Right-handed perspective projection mapping depth to [-1, 1] (OpenGL convention).
inline Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
    const float tanHalfFov = std::tan(fovYRadians / 2.0f);
    Mat4 result;
    result.at(0, 0) = 1.0f / (aspect * tanHalfFov);
    result.at(1, 1) = 1.0f / tanHalfFov;
    result.at(2, 2) = -(zFar + zNear) / (zFar - zNear);
    result.at(2, 3) = -(2.0f * zFar * zNear) / (zFar - zNear);
    result.at(3, 2) = -1.0f;
    return result;
}

// Right-handed view matrix looking from eye toward target.
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 result = Mat4::identity();
    result.at(0, 0) = s.x;
    result.at(0, 1) = s.y;
    result.at(0, 2) = s.z;
    result.at(1, 0) = u.x;
    result.at(1, 1) = u.y;
    result.at(1, 2) = u.z;
    result.at(2, 0) = -f.x;
    result.at(2, 1) = -f.y;
    result.at(2, 2) = -f.z;
    result.at(0, 3) = -dot(s, eye);
    result.at(1, 3) = -dot(u, eye);
    result.at(2, 3) = dot(f, eye);
    return result;
}

} // namespace viewer
