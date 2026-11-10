// Where the volcanoes of a world stand (volcano_placement.hpp). The draws of the settlement
// (settlement/settlement_random.hpp) before agent 45 moved them here, unchanged: each draw has its
// own stream, the FNV-1a of its name with the top bit set, so that the positions stay bit for bit
// those the settlement placed from data/settlement/world.json before.
#include "volcano_placement.hpp"

#include "planet/planet_parallel.hpp"
#include "point_spacing.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/json/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <vector>

namespace terrain {

namespace {

std::uint64_t streamOf(std::uint64_t seed, std::string_view name) {
    return noise::deriveSeed(seed, json::fnv1a64(name) | (std::uint64_t{1} << 63));
}

// A draw in [0, 1) of a stream, index by index.
double unitDraw(std::uint64_t stream, std::uint64_t index) {
    return static_cast<double>(noise::deriveSeed(stream, index) >> 11) * 0x1.0p-53;
}

// A count of the reference planet drawn in [low, high], each with the same chance, scaled to a
// world of scale times its area, rounded to the nearest whole number.
int drawCount(std::uint64_t seed, std::string_view name, const VolcanoParams::Count& range,
              double scale) {
    const auto span = static_cast<std::uint64_t>(range.max - range.min) + 1u;
    const int drawn =
        range.min + static_cast<int>(noise::deriveSeed(streamOf(seed, name), 0) % span);
    return static_cast<int>(std::floor(drawn * scale + 0.5));
}

// (1 - t^2)^2 inside [-1, 1], 0 outside.
double bump(double t) {
    if (std::abs(t) >= 1.0) {
        return 0.0;
    }
    const double u = 1.0 - t * t;
    return u * u;
}

// A ridge makes volcanoes within this share of its width, where the land is within kRidgeLand.
constexpr double kRidgeCore = 0.25;
constexpr double kRidgeLand = 200'000.0;
// A collision of foothills only (chainless) has half the arc.
constexpr double kChainlessArc = 0.5;
// Attempts to place a point at its spacing before giving it up.
constexpr int kAttempts = 24;

// The weight of each node of the map for the volcanoes of each origin.
struct NodeWeights {
    std::vector<double> arc;
    std::vector<double> rift;
    std::vector<double> ridge;
};

void weighNode(const GeographyPlates& plates, const VolcanoParams& params, int i, int j,
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
        if (plates.landShare(subducting) < 0.5 && km >= params.arcMinKm && km <= params.arcMaxKm) {
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

NodeWeights weighNodes(const GeographyPlates& plates, const VolcanoParams& params, int threads) {
    const geography::Grid& g = plates.grid();
    NodeWeights w;
    w.arc.assign(g.nodes(), 0.0);
    w.rift.assign(g.nodes(), 0.0);
    w.ridge.assign(g.nodes(), 0.0);
    planet::parallelRows(g.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                weighNode(plates, params, i, j, w);
            }
        }
    });
    return w;
}

// Draws count volcanoes of an origin on the nodes, each with the chance of its weight, jittered
// in the square of its node, kept apart by spacing. draw counts the draws of the stream.
void drawOnNodes(const GeographyPlates& plates, const std::vector<double>& weights, int count,
                 VolcanoOrigin origin, std::uint64_t stream, std::uint64_t& draw,
                 PointSpacing& spacing, std::vector<PlacedVolcano>& out) {
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
                out.push_back(PlacedVolcano{x, z, origin, false, false});
                break;
            }
        }
    }
}

// The first count volcanoes in the order of a stream get flag.
void markSome(std::vector<PlacedVolcano>& volcanoes, int count, std::uint64_t stream,
              bool PlacedVolcano::*flag) {
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

// The hotspots, and what the arcs and the rifts could not hold, anywhere.
void drawAnywhere(const WorldSize& world, int count, std::uint64_t stream, std::uint64_t& draw,
                  PointSpacing& spacing, std::vector<PlacedVolcano>& out) {
    for (int v = 0; v < count; ++v) {
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const double x = (unitDraw(stream, draw++) - 0.5) * world.x;
            const double z = (unitDraw(stream, draw++) - 0.5) * world.z;
            if (spacing.farEnough(x, z)) {
                spacing.add(x, z);
                out.push_back(PlacedVolcano{x, z, VolcanoOrigin::Hotspot, false, false});
                break;
            }
        }
    }
}

} // namespace

double volcanoScale(const WorldSize& world, const VolcanoParams& params) {
    return world.x * world.z / 1.0e6 / params.referenceAreaKm2;
}

std::vector<PlacedVolcano> placeVolcanoes(const GeographyPlates& plates, std::uint64_t worldSeed,
                                          const VolcanoParams& params, double scale,
                                          int threads) {
    const int total = drawCount(worldSeed, "volcanoes", params.volcanoes, scale);
    const int hotspots = static_cast<int>(std::floor(total * params.hotspotShare + 0.5));
    const int rifts = static_cast<int>(std::floor(total * params.riftShare + 0.5));
    const NodeWeights w = weighNodes(plates, params, threads);
    const double riftWeight = std::accumulate(w.rift.begin(), w.rift.end(), 0.0);
    const double ridgeWeight = std::accumulate(w.ridge.begin(), w.ridge.end(), 0.0);
    const int ridges = riftWeight + ridgeWeight > 0.0
                           ? static_cast<int>(std::floor(
                                 rifts * ridgeWeight / (riftWeight + ridgeWeight) + 0.5))
                           : 0;
    const WorldSize& world = plates.geography().world();
    PointSpacing spacing(params.spacingKm * 1000.0, world.x, world.z);
    std::vector<PlacedVolcano> volcanoes;
    const std::uint64_t stream = streamOf(worldSeed, "volcanoPositions");
    std::uint64_t draw = 0;
    drawOnNodes(plates, w.arc, total - hotspots - rifts, VolcanoOrigin::Arc, stream, draw,
                spacing, volcanoes);
    drawOnNodes(plates, w.rift, rifts - ridges, VolcanoOrigin::Rift, stream, draw, spacing,
                volcanoes);
    drawOnNodes(plates, w.ridge, ridges, VolcanoOrigin::Ridge, stream, draw, spacing, volcanoes);
    drawAnywhere(world, total - static_cast<int>(volcanoes.size()), stream, draw, spacing,
                 volcanoes);
    markSome(volcanoes, drawCount(worldSeed, "activeVolcanoes", params.active, scale),
             streamOf(worldSeed, "activeOrder"), &PlacedVolcano::active);
    markSome(volcanoes, drawCount(worldSeed, "largeVolcanoes", params.large, scale),
             streamOf(worldSeed, "largeOrder"), &PlacedVolcano::large);
    return volcanoes;
}

} // namespace terrain
