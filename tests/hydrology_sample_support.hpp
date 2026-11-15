#pragma once

// A sample of what the hydrology gives (agent 39), as FNV-1a checksums over the raw bits: the
// courses of reaches (Hydrology::reach), the queries at points (riversAt, clearanceAt,
// floorDistance, lakeAt, mayMatter) on lattices of 2 and 4 km (the pitches of the coarse levels of
// the horizon) and of 250 m near a fall, and pieces of the levels 8 and 9 of the horizon over the
// water. hydrology_bits_test pins them (the same bits before and after the work of agent 39);
// hydrology_threads_test computes them on 1 and 4 threads.

#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "map_world_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hydrology_sample {

using golden::Checksum;

// A reach in every kReachStride.
inline constexpr std::size_t kReachStride = 53;

inline void addWall(Checksum& sum, const terrain::SlotWall& w) {
    for (const float v : {w.distance, w.width, w.foot, w.top, w.water, w.airX, w.airZ, w.weight}) {
        sum.add(v);
    }
}

inline void addReach(Checksum& sum, const terrain::RiverReach& r) {
    sum.add(r.segment);
    sum.add(r.vertex);
    sum.add(r.discharge);
    sum.add(r.deepestCut);
    sum.add(static_cast<std::uint64_t>(r.points.size()));
    for (const terrain::RiverPoint& p : r.points) {
        sum.add(p.x);
        sum.add(p.z);
        for (const float v : {p.level, p.width, p.depth, p.floorWidth, p.gorge, p.mountain,
                              p.meander, p.valley, p.shift}) {
            sum.add(v);
        }
        sum.add(p.axisX);
        sum.add(p.axisZ);
    }
    sum.add(static_cast<std::uint64_t>(r.falls.size()));
    for (const terrain::RiverFall& f : r.falls) {
        for (const double v : {f.x, f.z, f.footX, f.footZ}) {
            sum.add(v);
        }
        for (const float v : {f.top, f.bottom, f.width, f.wetFrom, f.wetTo, f.pool}) {
            sum.add(v);
        }
        sum.add(static_cast<std::uint64_t>(f.edge), 1);
        for (std::size_t k = 0; k < terrain::kFallEdge; ++k) {
            for (const double v : {f.edgeX[k], f.edgeZ[k], f.footEdgeX[k], f.footEdgeZ[k]}) {
                sum.add(v);
            }
        }
    }
    sum.add(static_cast<std::uint64_t>(r.blocks.size()));
    for (const terrain::RiverBlock& b : r.blocks) {
        sum.add(b.x);
        sum.add(b.z);
        sum.add(b.radius);
        sum.add(b.top);
    }
}

inline void addCarve(Checksum& sum, const terrain::RiverCarve& c) {
    sum.add(c.carved);
    sum.add(c.height);
    sum.add(c.second);
    sum.add(c.wet);
    for (const float v : {c.level, c.floor, c.gorge, c.mountain, c.steps, c.pond}) {
        sum.add(v);
    }
    for (const double v : {c.upX, c.upZ, c.slope, c.above, c.raise, c.bank}) {
        sum.add(v);
    }
    sum.add(static_cast<std::uint64_t>(c.reach));
    addWall(sum, c.wall);
}

// The reaches of the sample: every kReachStride-th (falls, slots, lips and blocks among them).
inline std::vector<std::size_t> sampleReaches(const terrain::Hydrology& h) {
    std::vector<std::size_t> out;
    for (std::size_t r = 0; r < h.reachCount(); r += kReachStride) {
        out.push_back(r);
    }
    return out;
}

// The points of the sample: lattices of 2 and 4 km over boxes of 24 km around points of the river
// graph (one segment in kSegmentStride), and a lattice of 250 m over 6 km around the first fall of
// 20 m.
inline constexpr std::size_t kSegmentStride = 131;
inline std::vector<std::array<double, 2>> samplePoints(const terrain::PlanetLevel& planet) {
    std::vector<std::array<double, 2>> points;
    const auto& segments = planet.rivers().segments;
    for (std::size_t s = 0; s < segments.size(); s += kSegmentStride) {
        const terrain::RiverVertex& v = segments[s].vertices[segments[s].vertices.size() / 2];
        for (const double pitch : {2048.0, 4096.0}) {
            const int n = static_cast<int>(12'000.0 / pitch);
            for (int j = -n; j <= n; ++j) {
                for (int i = -n; i <= n; ++i) {
                    points.push_back({v.x + pitch * i + 0.25, v.z + pitch * j + 0.75});
                }
            }
        }
    }
    const map_world::FallPlace fall = map_world::firstFall();
    for (int j = -12; j <= 12; ++j) {
        for (int i = -12; i <= 12; ++i) {
            points.push_back({fall.x + 250.0 * i + 0.37, fall.z + 250.0 * j + 0.61});
        }
    }
    return points;
}

// What the queries give at a point, the relief before the valleys there.
inline void addPoint(Checksum& sum, const terrain::Hydrology& h, double x, double z) {
    const auto ground = static_cast<double>(h.relief().heightAt(x, z));
    addCarve(sum, h.riversAt(x, z, ground));
    const terrain::Hydrology::Clearance c = h.clearanceAt(x, z);
    sum.add(c.away);
    sum.add(c.floor);
    sum.add(h.floorDistance(x, z, 600.0));
    const terrain::LakeWater lake = h.lakeAt(x, z, ground);
    sum.add(lake.lake);
    sum.add(lake.level);
    sum.add(static_cast<std::uint64_t>(lake.kind), 1);
    sum.add(h.mayMatter(x, z));
}

// The horizon of the small world: 10 levels of 128 cells (its coarsest window fits in the world),
// the crests deferred as the GPU horizon computes its epochs.
inline terrain::HorizonParams horizonParams() {
    terrain::HorizonParams params;
    params.cellsPerSide = 128;
    params.crestRatio = 0;
    params.world = golden::kSmallPlanet;
    return params;
}

// The pieces of the sample: of the levels 8 and 9 of the epoch around a point of the river graph,
// one piece in kPieceStride, cut to its first kPieceRows rows.
inline constexpr std::size_t kPieceStride = 3;
inline constexpr std::int64_t kPieceRows = 2;

inline std::vector<terrain::HorizonPiece> samplePieces(const terrain::PlanetLevel& planet) {
    const auto& segments = planet.rivers().segments;
    const terrain::RiverVertex& v = segments[segments.size() / 2].vertices.front();
    const terrain::HorizonClipmap clipmap(horizonParams());
    const std::optional<terrain::HorizonEpoch> epoch = clipmap.plan(v.x, v.z);
    std::vector<terrain::HorizonPiece> out;
    std::array<std::size_t, 2> seen{};
    for (const terrain::HorizonPiece& p : epoch->pieces) {
        if (p.level < 8 || seen[static_cast<std::size_t>(p.level - 8)]++ % kPieceStride != 0) {
            continue;
        }
        terrain::HorizonPiece piece{p.level, p.rect, {}};
        piece.rect.z1 = std::min(piece.rect.z1, piece.rect.z0 + kPieceRows);
        out.push_back(piece);
    }
    return out;
}

inline void addCell(Checksum& sum, const terrain::HorizonCell& c) {
    sum.add(c.height);
    sum.add(c.gradX);
    sum.add(c.gradZ);
    sum.add(static_cast<std::uint64_t>(c.material), 1);
    sum.add(static_cast<std::uint64_t>(c.biome), 1);
    sum.add(static_cast<std::uint64_t>(c.flags), 1);
    sum.add(c.waterLevel);
    sum.add(static_cast<std::uint64_t>(c.blend.biomes), 4);
    sum.add(static_cast<std::uint64_t>(c.blend.materials), 4);
    sum.add(static_cast<std::uint64_t>(c.blend.biomeWeights), 4);
    sum.add(static_cast<std::uint64_t>(c.blend.materialWeights), 4);
}

} // namespace hydrology_sample
