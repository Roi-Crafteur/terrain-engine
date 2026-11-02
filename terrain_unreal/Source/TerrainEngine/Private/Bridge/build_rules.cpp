#include "build_rules.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

namespace {

// Distance from v to the interval [low, high], 0 inside.
double outside(double v, double low, double high) {
    return std::max({low - v, v - high, 0.0});
}

} // namespace

const char* gateRefusal(const BuildGate& gate) {
    if (!gate.playing) {
        return "not playing";
    }
    if (gate.devMapOpen) {
        return "the dev map is open";
    }
    if (gate.loading) {
        return "the view is loading";
    }
    if (!gate.controlsOwner) {
        return "the player does not control this pawn";
    }
    if (!gate.focused) {
        return "the game has no focus";
    }
    return nullptr;
}

bool EditRate::allow(double nowSeconds) {
    if (any_ && nowSeconds - last_ < interval_) {
        return false;
    }
    any_ = true;
    last_ = nowSeconds;
    return true;
}

CoreCapsule capsuleFromUnreal(const UnrealFrame& frame, const UnrealPoint& center,
                              double halfHeightCentimeters, double radiusCentimeters) {
    const CorePoint core = toCore(frame, center);
    const double half = halfHeightCentimeters / kCentimetersPerMeter;
    CoreCapsule capsule;
    capsule.x = core.x;
    capsule.z = core.z;
    capsule.bottom = core.y - half;
    capsule.top = core.y + half;
    capsule.radius = radiusCentimeters / kCentimetersPerMeter;
    return capsule;
}

double capsuleDistance(const VolumeEdit& edit, const CoreCapsule& capsule) {
    const BlockBox box = editBounds(edit);
    const double minX = box.min.x - 0.5;
    const double maxX = box.max.x + 0.5;
    const double minY = box.min.y - 0.5;
    const double maxY = box.max.y + 0.5;
    const double minZ = box.min.z - 0.5;
    const double maxZ = box.max.z + 0.5;
    // The axis of the capsule is the vertical segment between the centers of its caps: the
    // distance from a vertical segment to a box splits into its horizontal and vertical parts.
    const double low = capsule.bottom + capsule.radius;
    const double high = std::max(low, capsule.top - capsule.radius);
    const double dx = outside(capsule.x, minX, maxX);
    const double dz = outside(capsule.z, minZ, maxZ);
    const double dy = std::max({minY - high, low - maxY, 0.0});
    return std::sqrt(dx * dx + dy * dy + dz * dz) - capsule.radius;
}

bool touchesCapsule(const VolumeEdit& edit, const CoreCapsule& capsule) {
    return capsuleDistance(edit, capsule) < kCapsuleMarginMeters;
}

} // namespace terrain::unreal
