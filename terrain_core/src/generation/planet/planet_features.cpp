#include "terrain/generation/planet/planet_features.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>

namespace terrain::planet {

namespace {

// A point of the graph: segment and index of the point.
struct Point {
    std::int32_t segment = -1;
    std::int32_t vertex = 0;
};

class FeatureFinder {
public:
    FeatureFinder(const PlanetGrid& grid, const std::vector<float>& height,
                  const std::vector<std::uint8_t>& ocean, const RiverGraph& graph,
                  const PlanetFeatureParams& params, std::vector<RiverFeature>& out)
        : grid_(grid), height_(height), ocean_(ocean), graph_(graph), params_(params), out_(out) {}

    void mouth(std::int32_t s);
    void waterfalls(std::int32_t s);
    void gorges(std::int32_t s);

private:
    [[nodiscard]] const RiverVertex& at(Point p) const {
        return graph_.segments[static_cast<std::size_t>(p.segment)]
            .vertices[static_cast<std::size_t>(p.vertex)];
    }
    [[nodiscard]] std::int32_t mainUpstream(std::int32_t s) const;
    [[nodiscard]] bool previous(Point& p) const;
    [[nodiscard]] bool next(Point& p) const;
    [[nodiscard]] double slope(Point from, Point to) const;
    [[nodiscard]] double distance(Point from, Point to) const;
    [[nodiscard]] float wall(std::uint32_t cell, int dx, int dz) const;
    [[nodiscard]] bool landStep(const RiverSegment& segment, std::size_t v) const;

    const PlanetGrid& grid_;
    const std::vector<float>& height_;
    const std::vector<std::uint8_t>& ocean_;
    const RiverGraph& graph_;
    const PlanetFeatureParams& params_;
    std::vector<RiverFeature>& out_;
};

// The upstream segment of greatest discharge (then lowest index), or -1.
std::int32_t FeatureFinder::mainUpstream(std::int32_t s) const {
    std::int32_t best = -1;
    float discharge = -1.0f;
    for (const std::int32_t u : graph_.segments[static_cast<std::size_t>(s)].upstream) {
        const std::vector<RiverVertex>& v = graph_.segments[static_cast<std::size_t>(u)].vertices;
        const float q = v[v.size() - 2].discharge;
        if (q > discharge) {
            discharge = q;
            best = u;
        }
    }
    return best;
}

// The point above p along the river (across a confluence up the main branch).
bool FeatureFinder::previous(Point& p) const {
    if (p.vertex > 0) {
        --p.vertex;
        return true;
    }
    const std::int32_t up = mainUpstream(p.segment);
    if (up < 0) {
        return false;
    }
    p = {up,
         static_cast<std::int32_t>(graph_.segments[static_cast<std::size_t>(up)].vertices.size()) -
             2};
    return true;
}

// The point below p along the river (across a confluence into the segment joined).
bool FeatureFinder::next(Point& p) const {
    const RiverSegment* segment = &graph_.segments[static_cast<std::size_t>(p.segment)];
    if (static_cast<std::size_t>(p.vertex) + 1 == segment->vertices.size() &&
        segment->mouth == RiverMouth::Confluence) {
        p = {segment->downstream, 0}; // the same point, as the head of the segment joined
        segment = &graph_.segments[static_cast<std::size_t>(p.segment)];
    }
    if (static_cast<std::size_t>(p.vertex) + 1 < segment->vertices.size()) {
        ++p.vertex;
        return true;
    }
    return false;
}

double FeatureFinder::distance(Point from, Point to) const {
    // Consecutive points of the river are within one cell: their nearest images.
    const double dx = nearestImage(at(to).x, at(from).x, grid_.periodX()) - at(from).x;
    const double dz = nearestImage(at(to).z, at(from).z, grid_.periodZ()) - at(from).z;
    return dmath::hypot(dx, dz);
}

double FeatureFinder::slope(Point from, Point to) const {
    const double length = distance(from, to);
    return length > 0.0 ? (static_cast<double>(at(from).elevation) - at(to).elevation) / length
                        : 0.0;
}

// The step from point v to v + 1 stays on land (not into the sea or a lake).
bool FeatureFinder::landStep(const RiverSegment& segment, std::size_t v) const {
    const bool lastStep = v + 2 == segment.vertices.size();
    return !lastStep || segment.mouth == RiverMouth::Confluence ||
           segment.mouth == RiverMouth::ClosedBasin || segment.mouth == RiverMouth::Ice;
}

void FeatureFinder::mouth(std::int32_t s) {
    const RiverSegment& segment = graph_.segments[static_cast<std::size_t>(s)];
    if (segment.mouth != RiverMouth::Sea) {
        return;
    }
    const Point land{s, static_cast<std::int32_t>(segment.vertices.size()) - 2};
    const float discharge = at(land).discharge;
    if (discharge < std::min(params_.deltaMinDischarge, params_.estuaryMinDischarge)) {
        return;
    }
    // Mean slope of the last reach (or of the last step into the sea if nothing is above).
    Point top = land;
    double length = 0.0;
    for (int k = 0; k < params_.mouthReach; ++k) {
        Point above = top;
        if (!previous(above)) {
            break;
        }
        length += distance(above, top);
        top = above;
    }
    const double reachSlope =
        length > 0.0 ? (static_cast<double>(at(top).elevation) - at(land).elevation) / length
                     : slope(land, {s, land.vertex + 1});
    // Depth of the sea around the mouth: its cell and the sea cells next to it.
    const std::uint32_t sea = segment.vertices.back().cell;
    double depth = -static_cast<double>(height_[sea]);
    int count = 1;
    for (int k = 0; k < 8; ++k) {
        const std::size_t n = grid_.neighbor(sea, k);
        if (ocean_[n] != kLand) {
            depth -= height_[n];
            ++count;
        }
    }
    depth /= count;
    RiverFeature feature{RiverFeatureKind::Estuary,
                         s,
                         land.vertex,
                         land.vertex,
                         at(land).x,
                         at(land).z,
                         discharge,
                         static_cast<float>(reachSlope)};
    if (discharge >= params_.deltaMinDischarge && reachSlope <= params_.deltaMaxSlope &&
        depth <= params_.shelfDepth) {
        feature.kind = RiverFeatureKind::Delta;
    } else if (discharge < params_.estuaryMinDischarge) {
        return;
    }
    out_.push_back(feature);
}

void FeatureFinder::waterfalls(std::int32_t s) {
    const RiverSegment& segment = graph_.segments[static_cast<std::size_t>(s)];
    for (std::size_t v = 0; v + 1 < segment.vertices.size(); ++v) {
        const Point from{s, static_cast<std::int32_t>(v)};
        const RiverVertex& a = at(from);
        if (!landStep(segment, v) || a.discharge < params_.fallMinDischarge) {
            continue;
        }
        const Point to{s, from.vertex + 1};
        const double drop = static_cast<double>(a.elevation) - at(to).elevation;
        const double steep = slope(from, to);
        if (drop < params_.fallMinDrop || steep < params_.fallMinSlope) {
            continue;
        }
        // The steeper of the steps just above and just below. Above a lake outlet lies the flat
        // lake; above a plain source nothing is known, so no break of slope can be seen there.
        double around = 0.0;
        if (Point above = from; previous(above)) {
            around = std::max(around, slope(above, from));
        } else if (segment.sourceLakes.empty()) {
            continue;
        }
        if (Point below = to; next(below)) {
            around = std::max(around, slope(to, below));
        }
        if (steep >= params_.fallBreak * around) {
            const double mx = 0.5 * (a.x + nearestImage(at(to).x, a.x, grid_.periodX()));
            const double mz = 0.5 * (a.z + nearestImage(at(to).z, a.z, grid_.periodZ()));
            out_.push_back({RiverFeatureKind::Waterfall, s, from.vertex, from.vertex, mx, mz,
                            a.discharge, static_cast<float>(drop)});
        }
    }
}

// The highest ground within 2 cells of a cell along (dx, dz).
float FeatureFinder::wall(std::uint32_t cell, int dx, int dz) const {
    const int i = grid_.column(cell);
    const int j = grid_.row(cell);
    float highest = height_[grid_.index(grid_.wrapColumn(i + dx), grid_.wrapRow(j + dz))];
    highest = std::max(
        highest, height_[grid_.index(grid_.wrapColumn(i + 2 * dx), grid_.wrapRow(j + 2 * dz))]);
    return highest;
}

void FeatureFinder::gorges(std::int32_t s) {
    const RiverSegment& segment = graph_.segments[static_cast<std::size_t>(s)];
    RiverFeature gorge{RiverFeatureKind::Gorge, s, -1, -1, 0.0, 0.0, 0.0f, 0.0f};
    for (std::size_t v = 0; v + 1 < segment.vertices.size(); ++v) {
        const RiverVertex& a = segment.vertices[v];
        const RiverVertex& b = segment.vertices[v + 1];
        const int dx = (b.x > a.x) - (b.x < a.x);
        const int dz = (b.z > a.z) - (b.z < a.z);
        // Across the flow on both sides.
        const float depth = std::min(wall(a.cell, -dz, dx), wall(a.cell, dz, -dx)) - a.elevation;
        const bool deep =
            a.discharge >= params_.gorgeMinDischarge && depth >= params_.gorgeMinDepth;
        if (deep) {
            if (gorge.firstVertex < 0) {
                gorge.firstVertex = static_cast<std::int32_t>(v);
            }
            gorge.lastVertex = static_cast<std::int32_t>(v);
            if (depth > gorge.magnitude) {
                gorge.magnitude = depth;
                gorge.x = a.x;
                gorge.z = a.z;
                gorge.discharge = a.discharge;
            }
        }
        if ((!deep || v + 2 == segment.vertices.size()) && gorge.firstVertex >= 0) {
            out_.push_back(gorge);
            gorge = {RiverFeatureKind::Gorge, s, -1, -1, 0.0, 0.0, 0.0f, 0.0f};
        }
    }
}

} // namespace

std::vector<RiverFeature> findRiverFeatures(const PlanetGrid& grid,
                                            const std::vector<float>& height,
                                            const std::vector<std::uint8_t>& ocean,
                                            const RiverGraph& graph,
                                            const PlanetFeatureParams& params) {
    std::vector<RiverFeature> features;
    FeatureFinder finder(grid, height, ocean, graph, params, features);
    for (std::size_t s = 0; s < graph.segments.size(); ++s) {
        const auto segment = static_cast<std::int32_t>(s);
        finder.mouth(segment);
        finder.waterfalls(segment);
        finder.gorges(segment);
    }
    return features;
}

} // namespace terrain::planet
