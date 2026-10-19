#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

namespace {

constexpr Vec3 kWorldUp{0.0f, 1.0f, 0.0f};

} // namespace

Camera::Camera(Vec3 position) : position_(position) {}

void Camera::rotate(float dxPixels, float dyPixels) {
    yaw_ = std::remainder(yaw_ + dxPixels * kMouseSensitivity, 360.0f);
    pitch_ = std::clamp(pitch_ - dyPixels * kMouseSensitivity, -kMaxPitch, kMaxPitch);
}

void Camera::move(const MoveInput& input, float dtSeconds) {
    const Vec3 front = forward();
    const Vec3 right = normalize(cross(front, kWorldUp));
    const Vec3 direction = front * input.forward + right * input.right + kWorldUp * input.up;
    if (dot(direction, direction) == 0.0f) {
        return;
    }
    position_ = position_ + normalize(direction) * (kSpeed * dtSeconds);
}

Vec3 Camera::forward() const {
    const float yaw = radians(yaw_);
    const float pitch = radians(pitch_);
    return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
}

Mat4 Camera::viewMatrix() const {
    return lookAt(position_, position_ + forward(), kWorldUp);
}

} // namespace viewer
