#include "unreal_frame.hpp"

namespace terrain::unreal {

namespace {

constexpr auto kCentimetersPerMeterF = static_cast<float>(kCentimetersPerMeter);

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

} // namespace terrain::unreal
