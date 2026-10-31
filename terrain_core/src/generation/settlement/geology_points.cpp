#include "geology_points.hpp"

#include "../planet/planet_parallel.hpp"
#include "settlement_random.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace terrain::settlement {

namespace {

// (1 - t^2)^2 inside [-1, 1], 0 outside.
double bump(double t) {
    if (std::abs(t) >= 1.0) {
        return 0.0;
    }
    const double u = 1.0 - t * t;
    return u * u;
}

// A ridge makes volcanoes within this share of its width, where the land is within kRidgeLandKm.
constexpr double kRidgeCore = 0.25;
constexpr double kRidgeLand = 200'000.0;
// A collision of foothills only (chainless) has half the arc.
constexpr double kChainlessArc = 0.5;
// Attempts to place a point at its spacing before giving it up.
constexpr int kAttempts = 24;

// The weight of each node of the map for the volcanoes of each kind.
struct NodeWeights {
    std::vector<double> arc;
    std::vector<double> rift;
    std::vector<double> ridge;
};

void weighNode(const GeographyPlates& plates, const GeologyRule& rule, int i, int j,
               NodeWeights& w) {
    const GeographyPlates::Near n = plates.nearAtNode(i, j);
    if (n.boundary < 0) {
        return;
    }
    const geography::Boundary& b = plates.boundary(n.boundary);
    const std::size_t k = plates.grid().index(i, j);
    const GeographyParams& p = plates.geography().params();
    if (b.kind == BoundaryKind::Collision) {
        const int subducting = b.overriding == n.a ? n.b : n.a;
        const double s = b.overriding == n.b ? n.towardB : -n.towardB; // > 0 on the overriding side
        const double km = s / 1000.0;
        if (plates.landShare(subducting) < 0.5 && km >= rule.arcDistanceKm.min &&
            km <= rule.arcDistanceKm.max) {
            w.arc[k] = b.strength * (b.chainless ? kChainlessArc : 1.0);
        }
    } else if (b.kind == BoundaryKind::Divergence) {
        if (b.rift) {
            w.rift[k] = bump(n.towardB / p.riftWidth);
        } else if (plates.crust().coast[k] > -kRidgeLand) {
            w.ridge[k] = bump(n.towardB / (kRidgeCore * p.ridgeWidth));
        }
    }
}

NodeWeights weighNodes(const GeographyPlates& plates, const GeologyRule& rule, int threads) {
    const geography::Grid& g = plates.grid();
    NodeWeights w;
    w.arc.assign(g.nodes(), 0.0);
    w.rift.assign(g.nodes(), 0.0);
    w.ridge.assign(g.nodes(), 0.0);
    planet::parallelRows(g.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                weighNode(plates, rule, i, j, w);
            }
        }
    });
    return w;
}

// Draws count volcanoes of a kind on the nodes, each with the chance of its weight, jittered in
// the square of its node, kept apart by spacing. draw counts the draws of the stream.
void drawOnNodes(const GeographyPlates& plates, const std::vector<double>& weights, int count,
                 VolcanoKind kind, std::uint64_t stream, std::uint64_t& draw,
                 PointSpacing& spacing, std::vector<Volcano>& out) {
    std::vector<double> prefix(weights.size());
    std::partial_sum(weights.begin(), weights.end(), prefix.begin());
    const double total = prefix.empty() ? 0.0 : prefix.back();
    if (total <= 0.0) {
        return;
    }
    const geography::Grid& g = plates.grid();
    const WorldSize& world = plates.geography().world();
    for (int v = 0; v < count; ++v) {
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const double u = unitDraw(stream, draw++) * total;
            const auto node = static_cast<std::size_t>(
                std::upper_bound(prefix.begin(), prefix.end(), u) - prefix.begin());
            const std::size_t at = std::min(node, prefix.size() - 1);
            const int i = g.column(at);
            const int j = g.row(at);
            const double gx = g.x(i) + (unitDraw(stream, draw++) - 0.5) * g.sizeX;
            const double gz = g.z(j) + (unitDraw(stream, draw++) - 0.5) * g.sizeZ;
            const double x = wrapCoordinate(gx - plates.geography().anchorX(), world.x);
            const double z = wrapCoordinate(gz, world.z);
            if (spacing.farEnough(x, z)) {
                spacing.add(x, z);
                out.push_back(Volcano{x, z, kind, false, false});
                break;
            }
        }
    }
}

// The first count volcanoes in the order of a stream get flag.
void markSome(std::vector<Volcano>& volcanoes, int count, std::uint64_t stream,
              bool Volcano::*flag) {
    std::vector<std::size_t> order(volcanoes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [stream](std::size_t a, std::size_t b) {
        const std::uint64_t ka = noise::deriveSeed(stream, a);
        const std::uint64_t kb = noise::deriveSeed(stream, b);
        return ka < kb || (ka == kb && a < b);
    });
    const std::size_t n = std::min(order.size(), static_cast<std::size_t>(std::max(count, 0)));
    for (std::size_t k = 0; k < n; ++k) {
        volcanoes[order[k]].*flag = true;
    }
}

} // namespace

PointSpacing::PointSpacing(double minDistance, double periodX, double periodZ)
    : minDistance_(minDistance), periodX_(periodX), periodZ_(periodZ) {
    if (minDistance_ > 0.0) {
        countX_ = std::max(1, static_cast<int>(periodX_ / minDistance_));
        countZ_ = std::max(1, static_cast<int>(periodZ_ / minDistance_));
    }
}

int PointSpacing::bucketX(double x) const {
    const int b = static_cast<int>((wrapCoordinate(x, periodX_) / periodX_ + 0.5) * countX_);
    return std::min(b, countX_ - 1);
}

int PointSpacing::bucketZ(double z) const {
    const int b = static_cast<int>((wrapCoordinate(z, periodZ_) / periodZ_ + 0.5) * countZ_);
    return std::min(b, countZ_ - 1);
}

std::uint64_t PointSpacing::key(int bx, int bz) const {
    const int wx = ((bx % countX_) + countX_) % countX_;
    const int wz = ((bz % countZ_) + countZ_) % countZ_;
    return static_cast<std::uint64_t>(wz) * static_cast<std::uint64_t>(countX_) +
           static_cast<std::uint64_t>(wx);
}

bool PointSpacing::farEnough(double x, double z) const {
    if (minDistance_ <= 0.0) {
        return true;
    }
    const int bx = bucketX(x);
    const int bz = bucketZ(z);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto it = buckets_.find(key(bx + dx, bz + dz));
            if (it == buckets_.end()) {
                continue;
            }
            for (const auto& [px, pz] : it->second) {
                if (torusDistance(x, z, px, pz, periodX_, periodZ_) < minDistance_) {
                    return false;
                }
            }
        }
    }
    return true;
}

void PointSpacing::add(double x, double z) {
    if (minDistance_ > 0.0) {
        buckets_[key(bucketX(x), bucketZ(z))].emplace_back(x, z);
    }
}

std::vector<Volcano> placeVolcanoes(const GeographyPlates& plates, std::uint64_t worldSeed,
                                    const GeologyRule& rule, double scale, int threads) {
    const int total = drawCount(worldSeed, "volcanoes", rule.volcanoes, scale);
    const int hotspots = static_cast<int>(std::floor(total * rule.hotspotShare + 0.5));
    const int rifts = static_cast<int>(std::floor(total * rule.riftShare + 0.5));
    const NodeWeights w = weighNodes(plates, rule, threads);
    const double riftWeight = std::accumulate(w.rift.begin(), w.rift.end(), 0.0);
    const double ridgeWeight = std::accumulate(w.ridge.begin(), w.ridge.end(), 0.0);
    const int ridges = riftWeight + ridgeWeight > 0.0
                           ? static_cast<int>(std::floor(
                                 rifts * ridgeWeight / (riftWeight + ridgeWeight) + 0.5))
                           : 0;
    const WorldSize& world = plates.geography().world();
    PointSpacing spacing(rule.volcanoSpacingKm * 1000.0, world.x, world.z);
    std::vector<Volcano> volcanoes;
    const std::uint64_t stream = streamOf(worldSeed, "volcanoPositions");
    std::uint64_t draw = 0;
    drawOnNodes(plates, w.arc, total - hotspots - rifts, VolcanoKind::Arc, stream, draw, spacing,
                volcanoes);
    drawOnNodes(plates, w.rift, rifts - ridges, VolcanoKind::Rift, stream, draw, spacing,
                volcanoes);
    drawOnNodes(plates, w.ridge, ridges, VolcanoKind::Ridge, stream, draw, spacing, volcanoes);
    // Hotspots, and what the arcs and the rifts could not hold, anywhere.
    const auto missing = static_cast<int>(total - static_cast<int>(volcanoes.size()));
    for (int v = 0; v < missing; ++v) {
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const double x = (unitDraw(stream, draw++) - 0.5) * world.x;
            const double z = (unitDraw(stream, draw++) - 0.5) * world.z;
            if (spacing.farEnough(x, z)) {
                spacing.add(x, z);
                volcanoes.push_back(Volcano{x, z, VolcanoKind::Hotspot, false, false});
                break;
            }
        }
    }
    markSome(volcanoes, drawCount(worldSeed, "activeVolcanoes", rule.activeVolcanoes, scale),
             streamOf(worldSeed, "activeOrder"), &Volcano::active);
    markSome(volcanoes, drawCount(worldSeed, "largeVolcanoes", rule.largeVolcanoes, scale),
             streamOf(worldSeed, "largeOrder"), &Volcano::large);
    return volcanoes;
}

std::vector<ArcaneCrater> placeCraters(const WorldSize& world, std::uint64_t worldSeed,
                                       const GeologyRule& rule, double scale) {
    const int count = drawCount(worldSeed, "arcaneCraters", rule.arcaneCraters, scale);
    const int majors =
        std::min(count, drawCount(worldSeed, "majorCraters", rule.majorCraters, scale));
    PointSpacing spacing(2000.0 * rule.majorCraterRadiusKm.max, world.x, world.z);
    const std::uint64_t stream = streamOf(worldSeed, "craterPositions");
    std::uint64_t draw = 0;
    std::vector<ArcaneCrater> craters;
    for (int c = 0; c < count; ++c) {
        const bool major = c < majors;
        const double radius = drawIn(stream, draw++, major ? rule.majorCraterRadiusKm
                                                           : rule.craterRadiusKm);
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const double x = (unitDraw(stream, draw++) - 0.5) * world.x;
            const double z = (unitDraw(stream, draw++) - 0.5) * world.z;
            if (spacing.farEnough(x, z)) {
                spacing.add(x, z);
                craters.push_back(ArcaneCrater{x, z, radius, major});
                break;
            }
        }
    }
    return craters;
}

} // namespace terrain::settlement
