#include "terrain/io/relief_stats.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace terrain::relief_shapes {

namespace {

// A crossing of the coast with an edge of the grid, linked to the crossings of the squares on
// both sides of the edge.
struct Crossing {
    double x = 0.0;
    double z = 0.0;
    std::array<std::int64_t, 2> next{-1, -1};
    int links = 0;
};

using Crossings = std::map<std::int64_t, Crossing>;

struct Square {
    const ReliefGrid& grid;
    int i;
    int j;

    [[nodiscard]] int wrap(int v, int count) const {
        return grid.wraps ? (v % count + count) % count : v;
    }
    [[nodiscard]] float value(int di, int dj) const {
        return grid.at(wrap(i + di, grid.columns), wrap(j + dj, grid.rows));
    }
    // Edge ids: horizontal edge from corner (a, b) to (a + 1, b), vertical from (a, b) to
    // (a, b + 1).
    [[nodiscard]] std::int64_t edge(int di, int dj, bool vertical) const {
        const auto a = static_cast<std::int64_t>(wrap(i + di, grid.columns));
        const auto b = static_cast<std::int64_t>(wrap(j + dj, grid.rows));
        return 2 * (b * grid.columns + a) + (vertical ? 1 : 0);
    }
};

// Edge e of the square (0 top, 1 right, 2 bottom, 3 left): its id and its crossing.
std::pair<std::int64_t, Crossing> crossingOf(const Square& s, int e) {
    static constexpr std::array<std::array<int, 4>, 4> kCorners = {{
        {0, 0, 1, 0}, // top: corner (0, 0) to (1, 0)
        {1, 0, 1, 1}, // right
        {0, 1, 1, 1}, // bottom
        {0, 0, 0, 1}, // left
    }};
    const std::array<int, 4>& c = kCorners[static_cast<std::size_t>(e)];
    const float va = s.value(c[0], c[1]);
    const float vb = s.value(c[2], c[3]);
    const double t = static_cast<double>(va) / (static_cast<double>(va) - vb);
    const bool vertical = c[0] == c[2];
    Crossing crossing;
    const double ax = s.grid.x(s.wrap(s.i, s.grid.columns)) + c[0] * s.grid.spacing;
    const double az = s.grid.z(s.wrap(s.j, s.grid.rows)) + c[1] * s.grid.spacing;
    crossing.x = ax + (vertical ? 0.0 : t * s.grid.spacing);
    crossing.z = az + (vertical ? t * s.grid.spacing : 0.0);
    return {s.edge(c[0], c[1], vertical), crossing};
}

void link(Crossings& crossings, const Square& s, int e0, int e1) {
    const auto [id0, c0] = crossingOf(s, e0);
    const auto [id1, c1] = crossingOf(s, e1);
    Crossing& a = crossings.try_emplace(id0, c0).first->second;
    Crossing& b = crossings.try_emplace(id1, c1).first->second;
    if (a.links < 2 && b.links < 2) {
        a.next[static_cast<std::size_t>(a.links++)] = id1;
        b.next[static_cast<std::size_t>(b.links++)] = id0;
    }
}

// The segments of one square by marching squares; the saddles follow the mean of the corners.
void marchSquare(Crossings& crossings, const Square& s) {
    const std::array<bool, 4> land = {s.value(0, 0) >= 0.0f, s.value(1, 0) >= 0.0f,
                                      s.value(1, 1) >= 0.0f, s.value(0, 1) >= 0.0f};
    std::array<int, 4> edges{};
    int count = 0;
    for (int e = 0; e < 4; ++e) {
        if (land[static_cast<std::size_t>(e)] != land[static_cast<std::size_t>((e + 1) % 4)]) {
            edges[static_cast<std::size_t>(count++)] = e;
        }
    }
    if (count == 2) {
        link(crossings, s, edges[0], edges[1]);
    } else if (count == 4) {
        const float center = s.value(0, 0) + s.value(1, 0) + s.value(1, 1) + s.value(0, 1);
        // Corners 0 and 2 joined (land diagonal with land center, or sea diagonal with sea
        // center): cut around corners 1 and 3.
        if ((center >= 0.0f) == land[0]) {
            link(crossings, s, 0, 1);
            link(crossings, s, 2, 3);
        } else {
            link(crossings, s, 3, 0);
            link(crossings, s, 1, 2);
        }
    }
}

double nearestImage(double v, double reference, double period) {
    return period > 0.0 ? v - period * std::round((v - reference) / period) : v;
}

// Follows the links from start, marking the crossings visited.
Line follow(const Crossings& crossings, std::map<std::int64_t, bool>& visited, std::int64_t start,
            double periodX, double periodZ) {
    Line line;
    std::int64_t previous = -1;
    std::int64_t current = start;
    while (current >= 0 && !visited[current]) {
        visited[current] = true;
        const Crossing& c = crossings.at(current);
        const double x = line.x.empty() ? c.x : nearestImage(c.x, line.x.back(), periodX);
        const double z = line.z.empty() ? c.z : nearestImage(c.z, line.z.back(), periodZ);
        line.x.push_back(x);
        line.z.push_back(z);
        std::int64_t next = -1;
        for (int k = 0; k < c.links; ++k) {
            if (c.next[static_cast<std::size_t>(k)] != previous || c.links == 1) {
                next = c.next[static_cast<std::size_t>(k)];
                break;
            }
        }
        previous = current;
        current = next;
    }
    const Crossing& first = crossings.at(start);
    line.closed = line.x.size() > 2 && current == start;
    if (line.closed) {
        line.x.push_back(nearestImage(first.x, line.x.back(), periodX));
        line.z.push_back(nearestImage(first.z, line.z.back(), periodZ));
    }
    return line;
}

double distanceToSegment(double px, double pz, double ax, double az, double bx, double bz) {
    const double dx = bx - ax;
    const double dz = bz - az;
    const double length2 = dx * dx + dz * dz;
    double t = length2 > 0.0 ? ((px - ax) * dx + (pz - az) * dz) / length2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return dmath::hypot(px - (ax + t * dx), pz - (az + t * dz));
}

// The points of a line, extended past its end by a second turn when it is closed.
struct Path {
    const Line& line;
    std::size_t turn; // points in one turn (the closing point excluded when closed)

    [[nodiscard]] std::size_t limit() const { return line.closed ? 2 * turn : turn; }
    [[nodiscard]] std::pair<double, double> at(std::size_t k) const {
        if (k < turn || !line.closed) {
            return {line.x[k], line.z[k]};
        }
        const double shiftX = line.x[turn] - line.x[0];
        const double shiftZ = line.z[turn] - line.z[0];
        return {line.x[k - turn] + shiftX, line.z[k - turn] + shiftZ};
    }
};

bool straight(const Path& path, std::size_t i, std::size_t j, double tolerance, double relative) {
    const auto [ax, az] = path.at(i);
    const auto [bx, bz] = path.at(j);
    const double limit = std::max(tolerance, relative * dmath::hypot(bx - ax, bz - az));
    for (std::size_t k = i + 1; k < j; ++k) {
        const auto [px, pz] = path.at(k);
        if (distanceToSegment(px, pz, ax, az, bx, bz) > limit) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<Line> coastLines(const ReliefGrid& grid) {
    Crossings crossings;
    const int squaresX = grid.wraps ? grid.columns : grid.columns - 1;
    const int squaresZ = grid.wraps ? grid.rows : grid.rows - 1;
    for (int j = 0; j < squaresZ; ++j) {
        for (int i = 0; i < squaresX; ++i) {
            marchSquare(crossings, Square{grid, i, j});
        }
    }
    const double periodX = grid.wraps ? grid.columns * grid.spacing : 0.0;
    const double periodZ = grid.wraps ? grid.rows * grid.spacing : 0.0;
    std::map<std::int64_t, bool> visited;
    std::vector<Line> lines;
    // Open lines from their ends first, then the loops.
    for (const bool ends : {true, false}) {
        for (const auto& [id, crossing] : crossings) {
            if (!visited[id] && (!ends || crossing.links == 1)) {
                lines.push_back(follow(crossings, visited, id, periodX, periodZ));
            }
        }
    }
    return lines;
}

double lineLength(const Line& line) {
    double length = 0.0;
    for (std::size_t k = 1; k < line.x.size(); ++k) {
        length += dmath::hypot(line.x[k] - line.x[k - 1], line.z[k] - line.z[k - 1]);
    }
    return length;
}

StraightCoast longestStraight(const std::vector<Line>& lines, double tolerance, double relative) {
    StraightCoast best;
    for (const Line& line : lines) {
        const std::size_t turn = line.closed ? line.x.size() - 1 : line.x.size();
        if (turn < 2) {
            continue;
        }
        const Path path{line, turn};
        for (std::size_t i = 0; i < turn; ++i) {
            const std::size_t room = std::min(path.limit() - 1 - i, line.closed ? turn - 1 : turn);
            // Gallop then bisect on the number of points of the stretch.
            std::size_t good = 1;
            std::size_t bad = room + 1;
            while (good * 2 <= room && straight(path, i, i + good * 2, tolerance, relative)) {
                good *= 2;
            }
            bad = std::min(bad, good * 2);
            while (bad - good > 1) {
                const std::size_t mid = (good + bad) / 2;
                (straight(path, i, i + mid, tolerance, relative) ? good : bad) = mid;
            }
            const auto [ax, az] = path.at(i);
            const auto [bx, bz] = path.at(i + good);
            const double chord = dmath::hypot(bx - ax, bz - az);
            if (chord > best.length) {
                best = {chord, ax, az, bx, bz};
            }
        }
    }
    return best;
}

double dividerLength(const Line& line, double opening) {
    if (line.x.size() < 2 || !(opening > 0.0)) {
        return 0.0;
    }
    double cx = line.x[0];
    double cz = line.z[0];
    double t0 = 0.0;
    int steps = 0;
    for (std::size_t k = 1; k < line.x.size(); ++k) {
        const double ax = line.x[k - 1];
        const double az = line.z[k - 1];
        const double dx = line.x[k] - ax;
        const double dz = line.z[k] - az;
        // The first exit of the divider circle around the current point along the segment.
        for (;;) {
            const double fx = ax - cx;
            const double fz = az - cz;
            const double a = dx * dx + dz * dz;
            const double b = 2.0 * (fx * dx + fz * dz);
            const double c = fx * fx + fz * fz - opening * opening;
            const double disc = b * b - 4.0 * a * c;
            if (a <= 0.0 || disc < 0.0) {
                break;
            }
            const double t = (-b + std::sqrt(disc)) / (2.0 * a);
            if (t < t0 || t > 1.0) {
                break;
            }
            cx = ax + t * dx;
            cz = az + t * dz;
            t0 = t;
            ++steps;
        }
        t0 = 0.0;
    }
    return steps * opening + dmath::hypot(line.x.back() - cx, line.z.back() - cz);
}

} // namespace terrain::relief_shapes
