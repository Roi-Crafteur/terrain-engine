#include "unreal_frame.hpp"

#include <cmath>

namespace terrain::unreal {

namespace {

constexpr auto kCentimetersPerMeterF = static_cast<float>(kCentimetersPerMeter);

// The whole periods (centimeters, of the opposite sign) bringing a coordinate back within
// kWorldTurnThreshold of a period of 0: n = floor(|d| / period + 1 - threshold) leaves
// |d| - n period in [threshold - 1, threshold). Zero within the threshold, or for NaN.
double turnsBack(double centimeters, double period) {
    const double distance = std::abs(centimeters) / kCentimetersPerMeter;
    if (!(distance > kWorldTurnThreshold * period)) {
        return 0.0;
    }
    const double turns = std::floor(distance / period + (1.0 - kWorldTurnThreshold));
    const double back = turns * period * kCentimetersPerMeter;
    return centimeters > 0.0 ? -back : back;
}

} // namespace

UnrealPoint toUnreal(const UnrealFrame& frame, const CorePoint& point) {
    return {(point.x - frame.origin.x) * kCentimetersPerMeter,
            (point.z - frame.origin.z) * kCentimetersPerMeter,
            (point.y - frame.origin.y) * kCentimetersPerMeter};
}

CorePoint toCore(const UnrealFrame& frame, const UnrealPoint& point) {
    return {point.x / kCentimetersPerMeter + frame.origin.x,
            point.z / kCentimetersPerMeter + frame.origin.y,
            point.y / kCentimetersPerMeter + frame.origin.z};
}

UnrealVector3f toUnrealOffset(const Vec3f& meters) {
    return {meters.x * kCentimetersPerMeterF, meters.z * kCentimetersPerMeterF,
            meters.y * kCentimetersPerMeterF};
}

UnrealVector3f toUnrealDirection(const Vec3f& direction) {
    return {direction.x, direction.z, direction.y};
}

UnrealFrame shifted(const UnrealFrame& frame, const UnrealPoint& offset) {
    // toUnreal(p) + offset = 100 (p - origin') with origin' = origin - offset / 100, the axes of
    // offset swapped back to the core.
    return {{frame.origin.x - offset.x / kCentimetersPerMeter,
             frame.origin.y - offset.z / kCentimetersPerMeter,
             frame.origin.z - offset.y / kCentimetersPerMeter}};
}

ChunkCoord chunkAt(const UnrealFrame& frame, const UnrealPoint& point) {
    const CorePoint core = toCore(frame, point);
    return worldToChunk(core.x, core.z);
}

UnrealPoint worldTurnOffset(const UnrealPoint& position, const WorldSize& world) {
    // Unreal X is core x, Unreal Y is core z (toUnreal).
    return {turnsBack(position.x, world.x), turnsBack(position.y, world.z), 0.0};
}

} // namespace terrain::unreal
