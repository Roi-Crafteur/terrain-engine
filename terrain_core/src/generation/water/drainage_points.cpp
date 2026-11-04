// The points of the fine network (drainage_network.hpp): its sources, confluences and ends in a
// box, and what it is at a point.
#include "terrain/generation/water/drainage_network.hpp"

#include "drainage_impl.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

using water::kNoStep;
using water::NetworkNode;
using water::NodeOutlet;

namespace {

// How many channels of the 8 neighbors drain into node (gi, gj).
int channelDonors(const DrainageNetwork::Impl& m, std::int64_t gi, std::int64_t gj) {
    int donors = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const auto f = m.node(gi + dx, gj + dz);
            donors += f.node->channel && f.node->downX == -dx && f.node->downZ == -dz ? 1 : 0;
        }
    }
    return donors;
}

StreamEnd endOf(NodeOutlet outlet) {
    switch (outlet) {
    case NodeOutlet::Sea:
        return StreamEnd::Sea;
    case NodeOutlet::Lake:
        return StreamEnd::Lake;
    default:
        return StreamEnd::Confluence;
    }
}

} // namespace

std::vector<StreamMark> DrainageNetwork::marksIn(double minX, double minZ, double maxX,
                                                 double maxZ) const {
    const Impl& m = *impl_;
    std::vector<StreamMark> marks;
    const double cx = 0.5 * (minX + maxX);
    const double cz = 0.5 * (minZ + maxZ);
    const std::int64_t i0 = m.column(minX);
    const std::int64_t j0 = m.row(minZ);
    const auto wide = static_cast<std::int64_t>(std::ceil((maxX - minX) / water::kNodeSpacing));
    const auto tall = static_cast<std::int64_t>(std::ceil((maxZ - minZ) / water::kNodeSpacing));
    for (std::int64_t gj = j0; gj <= j0 + tall; ++gj) {
        for (std::int64_t gi = i0; gi <= i0 + wide; ++gi) {
            const NetworkNode& n = *m.node(gi, gj).node;
            if (!n.channel) {
                continue;
            }
            const std::array<double, 2> p = m.near(gi, gj, cx, cz);
            if (n.mainX == kNoStep) {
                marks.push_back({p[0], p[1], StreamMarkKind::Source, StreamEnd::Confluence, n.wet});
            } else if (channelDonors(m, gi, gj) >= 2) {
                marks.push_back({p[0], p[1], StreamMarkKind::Confluence, StreamEnd::Confluence, n.wet});
            }
            if (n.downX != kNoStep) {
                const NetworkNode& d = *m.node(gi + n.downX, gj + n.downZ).node;
                if (d.outlet != NodeOutlet::None) {
                    const std::array<double, 2> q = m.near(gi + n.downX, gj + n.downZ, cx, cz);
                    marks.push_back({q[0], q[1],
                                     d.outlet == NodeOutlet::Trunk ? StreamMarkKind::Confluence
                                                                   : StreamMarkKind::Mouth,
                                     endOf(d.outlet), n.wet});
                }
            }
        }
    }
    return marks;
}

NetworkPoint DrainageNetwork::at(double x, double z, double limit) const {
    const Impl& m = *impl_;
    NetworkPoint point;
    const std::int64_t ci = m.column(x);
    const std::int64_t cj = m.row(z);
    const auto reach = static_cast<std::int64_t>(std::ceil(limit / water::kNodeSpacing)) + 1;
    double best = limit;
    std::int64_t bi = 0;
    std::int64_t bj = 0;
    for (std::int64_t gj = cj - reach; gj <= cj + reach; ++gj) {
        for (std::int64_t gi = ci - reach; gi <= ci + reach; ++gi) {
            const NetworkNode& n = *m.node(gi, gj).node;
            if (!n.channel && n.outlet != NodeOutlet::Trunk) {
                continue;
            }
            const std::array<double, 2> p = m.near(gi, gj, x, z);
            const double d = std::hypot(p[0] - x, p[1] - z);
            if (d < best || (!point.stream && d <= limit)) {
                best = d;
                bi = gi;
                bj = gj;
                point.stream = true;
            }
        }
    }
    const NetworkNode& here = *m.node(ci, cj).node;
    point.permeability = here.permeability;
    point.surface = here.surface;
    point.sill = std::max(0.0f, here.filled - here.height);
    point.basin = m.basinOf(ci, cj);
    if (point.stream) {
        m.describe(bi, bj, x, z, best, point);
    }
    return point;
}

} // namespace terrain

namespace terrain {

std::uint32_t DrainageNetwork::Impl::basinOf(std::int64_t gi, std::int64_t gj) const {
    constexpr int kMostSteps = 20000;
    for (int step = 0; step < kMostSteps; ++step) {
        const auto f = node(gi, gj);
        const NetworkNode& n = *f.node;
        const std::size_t cell = planet->grid().index(f.block->ci, f.block->cj);
        if (n.outlet == NodeOutlet::Trunk) {
            return network.basin(n.trunkCell);
        }
        if (n.outlet == NodeOutlet::Sea) {
            return static_cast<std::uint32_t>(cell);
        }
        if (n.outlet != NodeOutlet::None || n.downX == kNoStep) {
            return network.basin(cell);
        }
        gi += n.downX;
        gj += n.downZ;
    }
    return network.basin(planet->grid().index(node(gi, gj).block->ci, node(gi, gj).block->cj));
}

void DrainageNetwork::Impl::describe(std::int64_t bi, std::int64_t bj, double x, double z,
                                     double distance, NetworkPoint& point) const {
    const NetworkNode& n = *node(bi, bj).node;
    const bool trunk = n.outlet == NodeOutlet::Trunk;
    point.distance = distance;
    point.discharge = dischargeOf(n);
    point.width = 7.0f * std::sqrt(std::max(point.discharge, 0.0f));
    point.depth = point.discharge > 0.0f
                      ? static_cast<float>(0.3 * dmath::pow(point.discharge, 0.4))
                      : 0.0f;
    point.order = orderOf(n);
    point.wet = trunk || n.wet;
    point.trunk = trunk;
    point.kind = trunk ? network.kind(n.trunkCell)
                       : (point.discharge >= 1.0f ? StreamKind::River : StreamKind::Brook);
    if (n.downX == kNoStep) {
        return;
    }
    const std::array<double, 2> p = near(bi, bj, x, z);
    const std::array<double, 2> d = near(bi + n.downX, bj + n.downZ, x, z);
    const double fx = d[0] - p[0];
    const double fz = d[1] - p[1];
    // Degrees clockwise from the north (-z).
    point.heading = std::fmod(dmath::atan2(fx, -fz) * 57.29577951308232 + 360.0, 360.0);
    // Looking downstream along (fx, fz), x east and z south, the right hand points to (-fz, fx).
    const double side = (x - p[0]) * -fz + (z - p[1]) * fx;
    point.bank = side > 0.0 ? StreamBank::Right : (side < 0.0 ? StreamBank::Left : StreamBank::None);
}

} // namespace terrain
