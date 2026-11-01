#include "terrain/generation/settlement/telluric.hpp"

#include "../geography_boundaries.hpp"
#include "geology_points.hpp"
#include "settlement_random.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr double kStep = kLineStepKm * 1000.0;
// The crossings are found on segments of kCoarse steps, hashed into buckets of kBucket meters.
constexpr int kCoarse = 6;
constexpr double kBucket = 400'000.0;
// A node at a crossing of two major lines, or of this intensity, is known by everyone.
constexpr double kVisibleIntensity = 0.8;
constexpr int kAttempts = 32;

// A smooth noise of one variable in [-1, 1]: values of the stream at the whole numbers, joined by
// smoothsteps.
double valueNoise(std::uint64_t stream, double t) {
    const double f = std::floor(t);
    const double u = t - f;
    const double s = u * u * (3.0 - 2.0 * u);
    const auto k = static_cast<std::uint64_t>(static_cast<std::int64_t>(f));
    const double a = 2.0 * unitDraw(stream, k) - 1.0;
    const double b = 2.0 * unitDraw(stream, k + 1u) - 1.0;
    return a + (b - a) * s;
}

TelluricLine traceLine(const WorldSize& world, std::uint64_t stream, const LineFamily& family,
                       const MagicRule& rule, bool major) {
    TelluricLine line;
    line.major = major;
    line.known = major ? Known::Visible : Known::Partial;
    double x = (unitDraw(stream, 0) - 0.5) * world.x;
    double z = (unitDraw(stream, 1) - 0.5) * world.z;
    const double heading = unitDraw(stream, 2);
    const double lengthKm = drawIn(stream, 3, family.lengthKm);
    line.widthKm = drawIn(stream, 4, family.widthKm);
    const double base = drawIn(stream, 5, family.intensity);
    const std::uint64_t turns = noise::deriveSeed(stream, 10);
    const std::uint64_t strength = noise::deriveSeed(stream, 11);
    const auto points = static_cast<std::size_t>(std::floor(lengthKm / kLineStepKm)) + 1u;
    for (std::size_t p = 0; p < std::max<std::size_t>(points, 2u); ++p) {
        const double km = static_cast<double>(p) * kLineStepKm;
        line.x.push_back(x);
        line.z.push_back(z);
        const double i = base * (0.75 + 0.25 * valueNoise(strength, 2.0 * km / rule.turnKm));
        line.intensity.push_back(static_cast<float>(std::clamp(i, 0.0, 1.0)));
        const double theta =
            heading + rule.turnDegrees / 360.0 * valueNoise(turns, km / rule.turnKm);
        const dmath::SineCosine sc = dmath::sinCosTurns(theta);
        x += kStep * sc.cosine;
        z += kStep * sc.sine;
    }
    return line;
}

// A coarse segment of a line, from point p to point p + kCoarse (or the last).
struct Segment {
    int line;
    std::size_t p;
    std::size_t q;
};

struct Crossing {
    double x;
    double z;
    double intensity;
    bool major;
};

// Where segments a and b cross, if they do: b moved by whole periods next to a first.
bool cross(const TelluricNetwork& n, const WorldSize& world, const Segment& a, const Segment& b,
           Crossing& out) {
    const TelluricLine& la = n.lines[static_cast<std::size_t>(a.line)];
    const TelluricLine& lb = n.lines[static_cast<std::size_t>(b.line)];
    const double ax = la.x[a.p];
    const double az = la.z[a.p];
    const double rx = la.x[a.q] - ax;
    const double rz = la.z[a.q] - az;
    const double shiftX = geography::shortOffset(
        wrapCoordinate(lb.x[b.p], world.x) - wrapCoordinate(ax, world.x), world.x);
    const double shiftZ = geography::shortOffset(
        wrapCoordinate(lb.z[b.p], world.z) - wrapCoordinate(az, world.z), world.z);
    const double bx = ax + shiftX;
    const double bz = az + shiftZ;
    const double sx = lb.x[b.q] - lb.x[b.p];
    const double sz = lb.z[b.q] - lb.z[b.p];
    const double denominator = rx * sz - rz * sx;
    if (denominator == 0.0) {
        return false;
    }
    const double t = ((bx - ax) * sz - (bz - az) * sx) / denominator;
    const double u = ((bx - ax) * rz - (bz - az) * rx) / denominator;
    if (t < 0.0 || t >= 1.0 || u < 0.0 || u >= 1.0) {
        return false;
    }
    out.x = wrapCoordinate(ax + t * rx, world.x);
    out.z = wrapCoordinate(az + t * rz, world.z);
    out.intensity = 0.5 * (la.intensity[a.p] + lb.intensity[b.p]);
    out.major = la.major && lb.major;
    return true;
}

std::uint64_t bucketOf(double x, double z, const WorldSize& world, int countX, int countZ) {
    const int bx = std::min(countX - 1, static_cast<int>((wrapCoordinate(x, world.x) / world.x +
                                                          0.5) * countX));
    const int bz = std::min(countZ - 1, static_cast<int>((wrapCoordinate(z, world.z) / world.z +
                                                          0.5) * countZ));
    return static_cast<std::uint64_t>(bz) * static_cast<std::uint64_t>(countX) +
           static_cast<std::uint64_t>(bx);
}

// Every crossing of two different lines, in the order of the segments.
std::vector<Crossing> crossings(const TelluricNetwork& n, const WorldSize& world) {
    std::vector<Segment> segments;
    for (std::size_t l = 0; l < n.lines.size(); ++l) {
        const std::size_t last = n.lines[l].x.size() - 1;
        for (std::size_t p = 0; p < last; p += kCoarse) {
            segments.push_back({static_cast<int>(l), p, std::min(p + kCoarse, last)});
        }
    }
    const int countX = std::max(1, static_cast<int>(world.x / kBucket));
    const int countZ = std::max(1, static_cast<int>(world.z / kBucket));
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets;
    for (std::size_t s = 0; s < segments.size(); ++s) {
        const TelluricLine& l = n.lines[static_cast<std::size_t>(segments[s].line)];
        buckets[bucketOf(l.x[segments[s].p], l.z[segments[s].p], world, countX, countZ)]
            .push_back(s);
    }
    std::vector<Crossing> out;
    for (std::size_t s = 0; s < segments.size(); ++s) {
        const TelluricLine& l = n.lines[static_cast<std::size_t>(segments[s].line)];
        const std::uint64_t home = bucketOf(l.x[segments[s].p], l.z[segments[s].p], world,
                                            countX, countZ);
        const int hx = static_cast<int>(home % static_cast<std::uint64_t>(countX));
        const int hz = static_cast<int>(home / static_cast<std::uint64_t>(countX));
        std::vector<std::size_t> near;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int bx = ((hx + dx) % countX + countX) % countX;
                const int bz = ((hz + dz) % countZ + countZ) % countZ;
                const auto it = buckets.find(static_cast<std::uint64_t>(bz) *
                                                 static_cast<std::uint64_t>(countX) +
                                             static_cast<std::uint64_t>(bx));
                if (it != buckets.end()) {
                    near.insert(near.end(), it->second.begin(), it->second.end());
                }
            }
        }
        std::sort(near.begin(), near.end());
        near.erase(std::unique(near.begin(), near.end()), near.end());
        for (const std::size_t t : near) {
            Crossing c{};
            if (segments[t].line > segments[s].line && cross(n, world, segments[s], segments[t], c)) {
                out.push_back(c);
            }
        }
    }
    return out;
}

void placeNodes(TelluricNetwork& n, const WorldSize& world, std::uint64_t seed,
                const MagicRule& rule, double scale) {
    const int target = drawCount(seed, "manaNodes", rule.nodes, scale);
    const std::uint64_t stream = streamOf(seed, "manaNodePositions");
    std::uint64_t draw = 0;
    PointSpacing spacing(2000.0 * rule.nodeRadiusKm.min, world.x, world.z);
    std::vector<Crossing> found = crossings(n, world);
    std::stable_sort(found.begin(), found.end(), [](const Crossing& a, const Crossing& b) {
        return a.intensity > b.intensity;
    });
    for (const Crossing& c : found) {
        if (static_cast<int>(n.nodes.size()) >= target || !spacing.farEnough(c.x, c.z)) {
            continue;
        }
        spacing.add(c.x, c.z);
        const double intensity = std::max(c.intensity, drawIn(stream, draw++, rule.nodeIntensity));
        const bool visible = c.major || intensity >= kVisibleIntensity;
        n.nodes.push_back({c.x, c.z, drawIn(stream, draw++, rule.nodeRadiusKm), intensity, true,
                           visible ? Known::Visible : Known::Partial});
    }
    // Secondary nodes along the lines, up to the count.
    while (static_cast<int>(n.nodes.size()) < target && !n.lines.empty()) {
        bool placed = false;
        for (int attempt = 0; attempt < kAttempts && !placed; ++attempt) {
            const TelluricLine& l =
                n.lines[static_cast<std::size_t>(drawBetween(stream, draw++, 0,
                                                             static_cast<int>(n.lines.size()) - 1))];
            const auto p = static_cast<std::size_t>(
                drawBetween(stream, draw++, 0, static_cast<int>(l.x.size()) - 1));
            const double x = wrapCoordinate(l.x[p], world.x);
            const double z = wrapCoordinate(l.z[p], world.z);
            if (spacing.farEnough(x, z)) {
                spacing.add(x, z);
                const double intensity = drawIn(stream, draw++, rule.nodeIntensity);
                n.nodes.push_back({x, z, drawIn(stream, draw++, rule.nodeRadiusKm), intensity,
                                   false, intensity >= kVisibleIntensity ? Known::Visible
                                                                         : Known::Partial});
                placed = true;
            }
        }
        if (!placed) {
            break; // no room left at this spacing
        }
    }
}

} // namespace

std::size_t TelluricNetwork::memoryBytes() const {
    std::size_t bytes = nodes.capacity() * sizeof(ManaNode) + deadZones.capacity() * sizeof(DeadZone);
    for (const TelluricLine& l : lines) {
        bytes += sizeof(TelluricLine) + l.x.capacity() * sizeof(double) +
                 l.z.capacity() * sizeof(double) + l.intensity.capacity() * sizeof(float);
    }
    return bytes;
}

TelluricNetwork buildTelluricNetwork(const WorldSize& world, std::uint64_t worldSeed,
                                     const MagicRule& rule, double scale) {
    TelluricNetwork n;
    const int majors = drawCount(worldSeed, "majorLines", rule.majorLines.count, scale);
    const int minors = drawCount(worldSeed, "minorLines", rule.minorLines.count, scale);
    const std::uint64_t majorStream = streamOf(worldSeed, "majorLinePaths");
    const std::uint64_t minorStream = streamOf(worldSeed, "minorLinePaths");
    for (int l = 0; l < majors; ++l) {
        n.lines.push_back(traceLine(world, noise::deriveSeed(majorStream, static_cast<std::uint64_t>(l)),
                                    rule.majorLines, rule, true));
    }
    for (int l = 0; l < minors; ++l) {
        n.lines.push_back(traceLine(world, noise::deriveSeed(minorStream, static_cast<std::uint64_t>(l)),
                                    rule.minorLines, rule, false));
    }
    placeNodes(n, world, worldSeed, rule, scale);
    const int dead = drawCount(worldSeed, "deadZones", rule.deadZones, scale);
    const std::uint64_t deadStream = streamOf(worldSeed, "deadZonePositions");
    for (int d = 0; d < dead; ++d) {
        const auto k = static_cast<std::uint64_t>(3 * d);
        n.deadZones.push_back({(unitDraw(deadStream, k) - 0.5) * world.x,
                               (unitDraw(deadStream, k + 1u) - 0.5) * world.z,
                               drawIn(deadStream, k + 2u, rule.deadZoneRadiusKm)});
    }
    return n;
}

} // namespace terrain::settlement
