#pragma once

#include "math.hpp"

namespace viewer {

// Movement intent along each camera axis, each component in [-1, 1].
struct MoveInput {
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
};

// Free-flying camera driven by mouse deltas and movement intents.
class Camera {
public:
    static constexpr float kSpeed = 10.0f;           // meters per second
    static constexpr float kMouseSensitivity = 0.1f; // degrees per pixel
    static constexpr float kMaxPitch = 89.0f;        // degrees

    explicit Camera(Vec3 position);

    // Mouse delta in screen pixels (y grows downward).
    void rotate(float dxPixels, float dyPixels);
    void move(const MoveInput& input, float dtSeconds);

    [[nodiscard]] Vec3 position() const { return position_; }
    [[nodiscard]] Vec3 forward() const;
    [[nodiscard]] Mat4 viewMatrix() const;

private:
    Vec3 position_;
    float yaw_ = -90.0f; // degrees; -90 looks toward -Z
    float pitch_ = 0.0f; // degrees
};

} // namespace viewer
