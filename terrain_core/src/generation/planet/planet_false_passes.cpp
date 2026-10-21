#include "terrain/generation/planet/planet_false_passes.hpp"

#include "planet_parallel.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terrain::planet {

namespace {

// Lengths of one segment in millimeters.
struct SegmentPasses {
    std::int64_t sampled = 0;
    std::int64_t over50 = 0;
    std::int64_t over200 = 0;
    std::int64_t over500 = 0;
    float maxExcess = 0.0f;
    double maxX = 0.0;
    double maxZ = 0.0;
};

SegmentPasses measureSegment(const RiverSegment& segment,
                             const std::function<float(double, double)>& relief, double step) {
    SegmentPasses result;
    double profileTop = segment.vertices[0].elevation;
    for (std::size_t v = 0; v + 1 < segment.vertices.size(); ++v) {
        const RiverVertex& a = segment.vertices[v];
        const RiverVertex& b = segment.vertices[v + 1];
        const double top = std::min(profileTop, static_cast<double>(a.elevation));
        const double bottom = std::min(top, static_cast<double>(b.elevation));
        profileTop = bottom;
        const double length = dmath::hypot(b.x - a.x, b.z - a.z);
        const double parts = std::max(1.0, std::ceil(length / step));
        const auto millimeters =
            static_cast<std::int64_t>(std::floor(length / parts * 1000.0 + 0.5));
        for (double m = 0.0; m < parts; m += 1.0) {
            const double t = (m + 0.5) / parts;
            const double x = a.x + (b.x - a.x) * t;
            const double z = a.z + (b.z - a.z) * t;
            const float excess = static_cast<float>(relief(x, z) - (top + (bottom - top) * t));
            result.sampled += millimeters;
            result.over50 += excess > 50.0f ? millimeters : 0;
            result.over200 += excess > 200.0f ? millimeters : 0;
            result.over500 += excess > 500.0f ? millimeters : 0;
            if (excess > result.maxExcess) {
                result.maxExcess = excess;
                result.maxX = x;
                result.maxZ = z;
            }
        }
    }
    return result;
}

} // namespace

FalsePassStats measureFalsePasses(const RiverGraph& graph,
                                  const std::function<float(double x, double z)>& relief,
                                  int threads, double step) {
    const auto count = static_cast<int>(graph.segments.size());
    std::vector<SegmentPasses> passes(graph.segments.size());
    parallelBlocks(count, 64, resolveThreads(threads), [&](int begin, int end) {
        for (int s = begin; s < end; ++s) {
            passes[static_cast<std::size_t>(s)] =
                measureSegment(graph.segments[static_cast<std::size_t>(s)], relief, step);
        }
    });
    FalsePassStats stats;
    std::int64_t sampled = 0;
    std::int64_t over50 = 0;
    std::int64_t over200 = 0;
    std::int64_t over500 = 0;
    std::array<std::int64_t, 8> byOrder{};
    std::array<std::int64_t, 8> over200ByOrder{};
    for (std::size_t s = 0; s < passes.size(); ++s) {
        const SegmentPasses& p = passes[s];
        sampled += p.sampled;
        over50 += p.over50;
        over200 += p.over200;
        over500 += p.over500;
        const std::size_t order = std::min<std::size_t>(graph.segments[s].strahler, 7);
        byOrder[order] += p.sampled;
        over200ByOrder[order] += p.over200;
        if (p.maxExcess > stats.maxExcess) {
            stats.maxExcess = p.maxExcess;
            stats.maxX = p.maxX;
            stats.maxZ = p.maxZ;
        }
    }
    stats.sampled = static_cast<double>(sampled) / 1000.0;
    stats.over50 = static_cast<double>(over50) / 1000.0;
    stats.over200 = static_cast<double>(over200) / 1000.0;
    stats.over500 = static_cast<double>(over500) / 1000.0;
    for (std::size_t k = 0; k < 8; ++k) {
        stats.sampledByOrder[k] = static_cast<double>(byOrder[k]) / 1000.0;
        stats.over200ByOrder[k] = static_cast<double>(over200ByOrder[k]) / 1000.0;
    }
    return stats;
}

} // namespace terrain::planet
