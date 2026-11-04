#include "terrain/generation/water/drainage_network.hpp"

#include "drainage_impl.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

using water::kNoStep;
using water::NetworkNode;
using water::NodeOutlet;

DrainageNetwork::DrainageNetwork(std::uint64_t seed, const Heightfield& relief,
                                 std::shared_ptr<const PlanetLevel> planet,
                                 const DrainageNetworkParams& params)
    : impl_(std::make_unique<Impl>(seed, relief, std::move(planet), params)) {}

DrainageNetwork::~DrainageNetwork() = default;

const PlanetNetwork& DrainageNetwork::planetNetwork() const {
    return impl_->network;
}

std::size_t DrainageNetwork::cacheBytes() const {
    return impl_->cache.bytes();
}

std::size_t DrainageNetwork::peakCacheBytes() const {
    return impl_->cache.peak();
}

std::size_t DrainageNetwork::blocksComputed() const {
    return impl_->cache.blocksMade();
}

namespace {

// The upstream node of a trunk node: a neighbor on a trunk whose way goes to it, or none.
bool trunkAbove(const DrainageNetwork::Impl& m, std::int64_t gi, std::int64_t gj,
                std::int64_t& ui, std::int64_t& uj) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const auto f = m.node(gi + dx, gj + dz);
            if (f.node->outlet == NodeOutlet::Trunk && f.node->downX == -dx &&
                f.node->downZ == -dz) {
                ui = gi + dx;
                uj = gj + dz;
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::vector<StreamPiece> DrainageNetwork::piecesIn(double minX, double minZ, double maxX,
                                                   double maxZ) const {
    const Impl& m = *impl_;
    std::vector<StreamPiece> pieces;
    const double cx = 0.5 * (minX + maxX);
    const double cz = 0.5 * (minZ + maxZ);
    const std::int64_t i0 = m.column(minX);
    const std::int64_t j0 = m.row(minZ);
    const auto wide = static_cast<std::int64_t>(std::ceil((maxX - minX) / water::kNodeSpacing));
    const auto tall = static_cast<std::int64_t>(std::ceil((maxZ - minZ) / water::kNodeSpacing));
    for (std::int64_t gj = j0 - 1; gj <= j0 + tall + 1; ++gj) {
        for (std::int64_t gi = i0 - 1; gi <= i0 + wide + 1; ++gi) {
            const auto f = m.node(gi, gj);
            const NetworkNode& n = *f.node;
            const bool trunk = n.outlet == NodeOutlet::Trunk;
            if ((!n.channel && !trunk) || n.downX == kNoStep) {
                continue;
            }
            const std::array<double, 2> p = m.near(gi, gj, cx, cz);
            const std::array<double, 2> d = m.near(gi + n.downX, gj + n.downZ, cx, cz);
            std::array<double, 2> u = p;
            std::int64_t ui = 0;
            std::int64_t uj = 0;
            if (!trunk && n.mainX != kNoStep) {
                u = m.near(gi + n.mainX, gj + n.mainZ, cx, cz);
            } else if (trunk && trunkAbove(m, gi, gj, ui, uj)) {
                u = m.near(ui, uj, cx, cz);
            }
            StreamPiece s;
            s.x0 = 0.5 * (u[0] + p[0]);
            s.z0 = 0.5 * (u[1] + p[1]);
            s.cx = p[0];
            s.cz = p[1];
            s.x1 = 0.5 * (p[0] + d[0]);
            s.z1 = 0.5 * (p[1] + d[1]);
            s.discharge = m.dischargeOf(n);
            s.width = 7.0f * std::sqrt(std::max(s.discharge, 0.0f));
            s.order = m.orderOf(n);
            s.wet = trunk || n.wet;
            s.trunk = trunk;
            pieces.push_back(s);
        }
    }
    return pieces;
}

} // namespace terrain
