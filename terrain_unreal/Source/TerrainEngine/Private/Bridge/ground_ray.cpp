#include "ground_ray.hpp"

#include <algorithm>

namespace terrain::unreal {

namespace {

constexpr double kLeastStep = 0.25;   // meters, near the camera
constexpr double kStepPerMeter = 0.002; // and 2 m more per km away
constexpr double kMostStep = 200.0;
constexpr double kPrecision = 0.01; // of the crossing, meters along the ray

CorePoint along(const CorePoint& origin, const CorePoint& direction, double t) {
    return {origin.x + direction.x * t, origin.y + direction.y * t, origin.z + direction.z * t};
}

} // namespace

CorePoint coreDirection(double unrealX, double unrealY, double unrealZ) {
    return {unrealX, unrealZ, unrealY};
}

std::optional<GroundHit> marchGround(const CorePoint& origin, const CorePoint& direction,
                                     double maxDistance,
                                     const std::function<double(double x, double z)>& height,
                                     int maxSamples) {
    GroundHit hit;
    const auto clearance = [&](double t) {
        const CorePoint p = along(origin, direction, t);
        ++hit.samples;
        return p.y - height(p.x, p.z);
    };
    double above = 0.0;
    double gap = clearance(above);
    if (gap <= 0.0) {
        hit.point = origin;
        return hit;
    }
    double below = above;
    bool crossed = false;
    while (above < maxDistance && hit.samples < maxSamples) {
        const double step = std::clamp(0.5 * gap, kLeastStep + kStepPerMeter * above, kMostStep);
        const double next = std::min(above + step, maxDistance);
        const double nextGap = clearance(next);
        if (nextGap <= 0.0) {
            below = next;
            crossed = true;
            break;
        }
        above = next;
        gap = nextGap;
    }
    if (!crossed) {
        return std::nullopt;
    }
    while (below - above > kPrecision) {
        const double middle = 0.5 * (above + below);
        (clearance(middle) > 0.0 ? above : below) = middle;
    }
    hit.distance = below;
    hit.point = along(origin, direction, below);
    hit.point.y = height(hit.point.x, hit.point.z);
    return hit;
}

std::optional<TileKey> shownTileAt(const TileStreamer& streamer, ChunkCoord chunk) {
    const WorldSize& world = streamer.manager().params().world;
    const ChunkCoord canonical = wrapChunk(chunk, world);
    for (int lod = 0; lod < kLodCount; ++lod) {
        const TileKey key = wrapTile(tileContaining(canonical, lod), world);
        if (streamer.shown(key)) {
            return key;
        }
    }
    return std::nullopt;
}

} // namespace terrain::unreal
