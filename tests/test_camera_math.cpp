#include "camera.hpp"
#include "math.hpp"

#include "test_assert.hpp"

#include <cmath>

namespace {

using viewer::Camera;
using viewer::Mat4;
using viewer::Vec3;

constexpr float kEpsilon = 1e-4f;

bool approx(float a, float b) {
    return std::abs(a - b) < kEpsilon;
}

bool approx(Vec3 a, Vec3 b) {
    return approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z);
}

Vec3 transformPoint(const Mat4& m, Vec3 p) {
    const auto row = [&](int r) {
        return m.at(r, 0) * p.x + m.at(r, 1) * p.y + m.at(r, 2) * p.z + m.at(r, 3);
    };
    const float w = row(3);
    return {row(0) / w, row(1) / w, row(2) / w};
}

void testPerspectiveMapsNearAndFarPlanes() {
    const Mat4 p = viewer::perspective(viewer::radians(90.0f), 2.0f, 1.0f, 3.0f);
    TEST_CHECK(approx(p.at(0, 0), 0.5f));
    TEST_CHECK(approx(p.at(1, 1), 1.0f));
    TEST_CHECK(approx(p.at(3, 2), -1.0f));
    TEST_CHECK(approx(p.at(3, 3), 0.0f));
    TEST_CHECK(approx(transformPoint(p, {0.0f, 0.0f, -1.0f}).z, -1.0f));
    TEST_CHECK(approx(transformPoint(p, {0.0f, 0.0f, -3.0f}).z, 1.0f));
}

void testLookAt() {
    const Vec3 eye{1.0f, 2.0f, 3.0f};
    const Mat4 view = viewer::lookAt(eye, {1.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    TEST_CHECK(approx(transformPoint(view, eye), {0.0f, 0.0f, 0.0f}));
    TEST_CHECK(approx(transformPoint(view, {1.0f, 2.0f, 2.0f}), {0.0f, 0.0f, -1.0f}));
    TEST_CHECK(approx(transformPoint(view, {2.0f, 2.0f, 3.0f}), {1.0f, 0.0f, 0.0f}));
}

void testMatrixProduct() {
    const Mat4 view = viewer::lookAt({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const Mat4 projection = viewer::perspective(viewer::radians(60.0f), 1.5f, 0.1f, 100.0f);
    TEST_CHECK((Mat4::identity() * view).m == view.m);
    const Vec3 point{0.3f, -0.2f, 0.1f};
    TEST_CHECK(approx(transformPoint(projection * view, point),
                      transformPoint(projection, transformPoint(view, point))));
}

void testCameraLooksTowardNegativeZByDefault() {
    const Camera camera({0.0f, 0.0f, 0.0f});
    TEST_CHECK(approx(camera.forward(), {0.0f, 0.0f, -1.0f}));
    TEST_CHECK(approx(transformPoint(camera.viewMatrix(), camera.position()), {0.0f, 0.0f, 0.0f}));
}

void testCameraRotation() {
    Camera camera({0.0f, 0.0f, 0.0f});
    camera.rotate(90.0f / Camera::kMouseSensitivity, 0.0f);
    TEST_CHECK(approx(camera.forward(), {1.0f, 0.0f, 0.0f}));

    camera.rotate(0.0f, -1e6f);
    TEST_CHECK(approx(camera.forward().y, std::sin(viewer::radians(Camera::kMaxPitch))));
    camera.rotate(0.0f, 1e6f);
    TEST_CHECK(approx(camera.forward().y, -std::sin(viewer::radians(Camera::kMaxPitch))));
}

void testCameraMovement() {
    Camera camera({0.0f, 0.0f, 0.0f});
    camera.move({.forward = 1.0f}, 0.5f);
    TEST_CHECK(approx(camera.position(), {0.0f, 0.0f, -0.5f * Camera::kSpeed}));

    camera.move({}, 1.0f);
    TEST_CHECK(approx(camera.position(), {0.0f, 0.0f, -0.5f * Camera::kSpeed}));

    Camera diagonal({0.0f, 0.0f, 0.0f});
    diagonal.move({.forward = 1.0f, .right = 1.0f, .up = 1.0f}, 1.0f);
    TEST_CHECK(approx(viewer::length(diagonal.position()), Camera::kSpeed));
}

} // namespace

int main() {
    testPerspectiveMapsNearAndFarPlanes();
    testLookAt();
    testMatrixProduct();
    testCameraLooksTowardNegativeZByDefault();
    testCameraRotation();
    testCameraMovement();
    return test::result();
}
