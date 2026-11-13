#include "block_targeting.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace terrain::unreal {

namespace {

std::int32_t nearestLattice(double v) {
    return static_cast<std::int32_t>(std::floor(v + 0.5));
}

CoreDirection unitOrUp(const CoreDirection& normal) {
    const double length =
        std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(length) || length <= 0.0) {
        return {0.0, 1.0, 0.0};
    }
    return {normal.x / length, normal.y / length, normal.z / length};
}

BlockCoord offset(BlockCoord block, BlockCoord step) {
    return {block.x + step.x, block.y + step.y, block.z + step.z};
}

BuildGhost blockGhost(BlockCoord block) {
    BuildGhost ghost;
    ghost.center = {static_cast<double>(block.x), static_cast<double>(block.y),
                    static_cast<double>(block.z)};
    ghost.halfX = ghost.halfY = ghost.halfZ = 0.5;
    return ghost;
}

} // namespace

const char* toolName(BuildTool tool) {
    switch (tool) {
    case BuildTool::Sphere2:
        return "sphere 3 m";
    case BuildTool::Sphere4:
        return "sphere 5 m";
    case BuildTool::Block:
        break;
    }
    return "block 1 m";
}

std::string shapeName(BuildTool tool, double radius) {
    if (tool == BuildTool::Block) {
        return toolName(tool);
    }
    for (const BuildTool sphere : {BuildTool::Sphere2, BuildTool::Sphere4}) {
        if (std::lround(radius * 16.0) == std::lround(toolRadius(sphere) * 16.0)) {
            return toolName(sphere);
        }
    }
    char text[64];
    std::snprintf(text, sizeof(text), "sphere of radius %.4g m",
                  static_cast<double>(std::lround(radius * 16.0)) / 16.0);
    return text;
}

double toolRadius(BuildTool tool) {
    switch (tool) {
    case BuildTool::Sphere2:
        return 26.0 / 16.0;
    case BuildTool::Sphere4:
        return 39.0 / 16.0;
    case BuildTool::Block:
        break;
    }
    return 0.0;
}

CoreDirection toCoreDirection(double x, double y, double z) {
    return {x, z, y};
}

UnrealPoint toUnrealDirection(const CoreDirection& direction) {
    return {direction.x, direction.z, direction.y};
}

BlockCoord dominantFace(const CoreDirection& normal) {
    const CoreDirection n = unitOrUp(normal);
    const double ax = std::fabs(n.x);
    const double ay = std::fabs(n.y);
    const double az = std::fabs(n.z);
    if (ay >= ax && ay >= az) {
        return {0, n.y < 0.0 ? -1 : 1, 0};
    }
    if (ax >= az) {
        return {n.x < 0.0 ? -1 : 1, 0, 0};
    }
    return {0, 0, n.z < 0.0 ? -1 : 1};
}

BuildHit hitFromUnreal(const UnrealFrame& frame, const UnrealPoint& point, double normalX,
                       double normalY, double normalZ, double distanceCentimeters) {
    BuildHit hit;
    hit.point = toCore(frame, point);
    hit.normal = toCoreDirection(normalX, normalY, normalZ);
    hit.distance = distanceCentimeters / kCentimetersPerMeter;
    return hit;
}

BlockCoord blockOfPoint(const CorePoint& point) {
    return {nearestLattice(point.x), nearestLattice(point.y), nearestLattice(point.z)};
}

BuildTarget buildTarget(const BuildHit& hit, BuildTool tool, BuildAction action) {
    return buildTarget(hit, tool, action, toolRadius(tool));
}

BuildTarget buildTarget(const BuildHit& hit, BuildTool tool, BuildAction action, double radius) {
    const CoreDirection n = unitOrUp(hit.normal);
    BuildTarget target;
    target.edit.mode = action == BuildAction::Break ? EditMode::Remove : EditMode::Add;
    if (tool == BuildTool::Block) {
        const BlockCoord broken = blockOfPoint({hit.point.x - n.x * kBreakDepthMeters,
                                                hit.point.y - n.y * kBreakDepthMeters,
                                                hit.point.z - n.z * kBreakDepthMeters});
        const BlockCoord block =
            action == BuildAction::Break ? broken : offset(broken, dominantFace(n));
        target.edit.shape = EditShape::Box;
        target.edit.anchor = block;
        target.edit.size = {1, 1, 1};
        target.ghost = blockGhost(block);
        return target;
    }
    const double out = action == BuildAction::Break ? 0.0 : radius * 0.5;
    const BlockCoord center = blockOfPoint(
        {hit.point.x + n.x * out, hit.point.y + n.y * out, hit.point.z + n.z * out});
    target.edit.shape = EditShape::Sphere;
    target.edit.anchor = center;
    target.edit.radius = static_cast<std::uint16_t>(std::lround(radius * 16.0));
    target.ghost.sphere = true;
    target.ghost.center = {static_cast<double>(center.x), static_cast<double>(center.y),
                           static_cast<double>(center.z)};
    target.ghost.halfX = target.ghost.halfY = target.ghost.halfZ = radius;
    return target;
}

bool withinReach(const BuildHit& hit, double reachMeters) {
    return std::isfinite(hit.distance) && hit.distance >= 0.0 && hit.distance <= reachMeters;
}

bool aimsAt(const BuildHit& hit, const CorePoint& point) {
    const double dx = hit.point.x - point.x;
    const double dy = hit.point.y - point.y;
    const double dz = hit.point.z - point.z;
    return dx * dx + dy * dy + dz * dz <= kAimToleranceMeters * kAimToleranceMeters;
}

UnrealGhost toUnrealGhost(const UnrealFrame& frame, const BuildGhost& ghost) {
    UnrealGhost out;
    out.sphere = ghost.sphere;
    out.center = toUnreal(frame, ghost.center);
    out.half = {ghost.halfX * kCentimetersPerMeter, ghost.halfZ * kCentimetersPerMeter,
                ghost.halfY * kCentimetersPerMeter};
    return out;
}

} // namespace terrain::unreal
