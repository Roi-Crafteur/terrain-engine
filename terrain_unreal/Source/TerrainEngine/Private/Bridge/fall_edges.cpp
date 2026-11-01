#include "fall_edges.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace terrain::unreal {

namespace {

// Beyond the ends of an edge (meters across): the water at the sides of its channel still fades
// toward it.
constexpr double kEdgeMargin = 2.5;

} // namespace

FallEdges::FallEdges(const TileData& data) {
    for (const TileFall& f : data.falls) {
        const double dx = f.footX - f.x;
        const double dz = f.footZ - f.z;
        const double run = dmath::hypot(dx, dz);
        if (!(run > 0.0) || !(f.top > f.bottom)) {
            continue;
        }
        Edge e;
        e.lipX = f.x - data.originX;
        e.lipZ = f.z - data.originZ;
        e.dirX = dx / run;
        e.dirZ = dz / run;
        e.top = f.top;
        e.bottom = f.bottom;
        const double acrossX = -e.dirZ;
        const double acrossZ = e.dirX;
        for (std::size_t p = 0; p < kFallEdge; ++p) {
            if (f.edge == kFallEdge) {
                e.x[p] = f.edgeX[p] - data.originX;
                e.z[p] = f.edgeZ[p] - data.originZ;
                e.footX[p] = f.footEdgeX[p] - data.originX;
                e.footZ[p] = f.footEdgeZ[p] - data.originZ;
            } else {
                // A straight lip across its channel, at its lip and at its foot.
                const double w = 0.5 * static_cast<double>(f.width) *
                                 (2.0 * static_cast<double>(p) / (kFallEdge - 1.0) - 1.0);
                e.x[p] = e.lipX + acrossX * w;
                e.z[p] = e.lipZ + acrossZ * w;
                e.footX[p] = e.lipX + dx + acrossX * w;
                e.footZ[p] = e.lipZ + dz + acrossZ * w;
            }
            e.across[p] = (e.x[p] - e.lipX) * acrossX + (e.z[p] - e.lipZ) * acrossZ;
            e.footAcross[p] = (e.footX[p] - e.lipX) * acrossX + (e.footZ[p] - e.lipZ) * acrossZ;
        }
        edges_.push_back(e);
    }
}

std::optional<double> FallEdges::downstreamOf(const Edge& e,
                                              const std::array<double, kFallEdge>& px,
                                              const std::array<double, kFallEdge>& pz,
                                              const std::array<double, kFallEdge>& across,
                                              double x, double z, double margin) {
    const double a = (x - e.lipX) * -e.dirZ + (z - e.lipZ) * e.dirX;
    const bool rising = across[kFallEdge - 1] >= across[0];
    const double low = rising ? across[0] : across[kFallEdge - 1];
    const double high = rising ? across[kFallEdge - 1] : across[0];
    if (!(a >= low - margin) || !(a <= high + margin)) {
        return std::nullopt;
    }
    // The point of the line at a: on the segment holding it (the ends beyond them).
    std::size_t p = 0;
    while (p + 2 < kFallEdge && (rising ? across[p + 1] < a : across[p + 1] > a)) {
        ++p;
    }
    const double span = across[p + 1] - across[p];
    const double t = span != 0.0 ? std::clamp((a - across[p]) / span, 0.0, 1.0) : 0.0;
    const double qx = px[p] + t * (px[p + 1] - px[p]);
    const double qz = pz[p] + t * (pz[p + 1] - pz[p]);
    return (x - qx) * e.dirX + (z - qz) * e.dirZ;
}

std::optional<double> FallEdges::toward(std::size_t n, double x, double z) const {
    const Edge& e = edges_[n];
    const std::optional<double> down = downstreamOf(e, e.x, e.z, e.across, x, z, kEdgeMargin);
    if (!down) {
        return std::nullopt;
    }
    return std::clamp((kEdgeOver - *down) / kEdgeFade, kEdgeBeyond, kEdgeAbove);
}

bool FallEdges::upper(std::size_t n, double level, double tolerance) const {
    const Edge& e = edges_[n];
    return level >= e.top - tolerance && level <= e.top + kUpperRise &&
           level > 0.5 * (e.top + e.bottom);
}

std::optional<double> FallEdges::above(double x, double z, double level, double tolerance,
                                       double reach) const {
    std::optional<double> fade;
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t n = 0; n < edges_.size(); ++n) {
        const Edge& e = edges_[n];
        if (!upper(n, level, tolerance)) {
            continue;
        }
        const std::optional<double> down = downstreamOf(e, e.x, e.z, e.across, x, z, kEdgeMargin);
        if (down && std::abs(*down) <= reach && std::abs(*down) < nearest) {
            nearest = std::abs(*down);
            fade = std::clamp((kEdgeOver - *down) / kEdgeFade, kEdgeBeyond, kEdgeAbove);
        }
    }
    return fade;
}

bool FallEdges::face(double x, double z, double level, double tolerance, double margin) const {
    for (const Edge& e : edges_) {
        if (!(level > e.bottom + tolerance) || !(level < e.top - tolerance)) {
            continue;
        }
        const std::optional<double> lip = downstreamOf(e, e.x, e.z, e.across, x, z, kEdgeMargin);
        const std::optional<double> foot =
            downstreamOf(e, e.footX, e.footZ, e.footAcross, x, z, kEdgeMargin);
        if (lip && foot && *lip >= -margin && *foot <= margin) {
            return true;
        }
    }
    return false;
}

bool FallEdges::within(double x, double z, double margin) const {
    for (const Edge& e : edges_) {
        const std::optional<double> lip = downstreamOf(e, e.x, e.z, e.across, x, z, kEdgeMargin);
        const std::optional<double> foot =
            downstreamOf(e, e.footX, e.footZ, e.footAcross, x, z, kEdgeMargin);
        if (lip && foot && *lip >= -margin && *foot <= margin) {
            return true;
        }
    }
    return false;
}

bool FallEdges::belowOf(std::size_t n, double x, double z, double level, double tolerance,
                        double reach) const {
    const Edge& e = edges_[n];
    if (!(std::abs(level - e.bottom) <= tolerance)) {
        return false;
    }
    const std::optional<double> down =
        downstreamOf(e, e.footX, e.footZ, e.footAcross, x, z, kEdgeMargin);
    return down && *down >= -reach && *down <= reach;
}

bool FallEdges::below(double x, double z, double level, double tolerance, double reach) const {
    for (std::size_t n = 0; n < edges_.size(); ++n) {
        if (belowOf(n, x, z, level, tolerance, reach)) {
            return true;
        }
    }
    return false;
}

} // namespace terrain::unreal
