#include "voronoi_layout.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <numbers>
#include <utility>

namespace terrain::voronoi_layout {

namespace {

// Uniform draws: the k-th draw hashes (seed, k), so the layout never depends on the random
// library of the platform.
class Random {
public:
    explicit Random(std::uint64_t seed) : seed_(seed) {}

    // In [0, 1).
    double unit() {
        const std::uint64_t bits = noise::deriveSeed(seed_, counter_++);
        return static_cast<double>(bits >> 11) * 0x1.0p-53;
    }

    // In [0, n), for n >= 1.
    int below(int n) { return std::min(n - 1, static_cast<int>(unit() * n)); }

private:
    std::uint64_t seed_;
    std::uint64_t counter_ = 0;
};

// Independent streams of the layout. New streams go at the end.
enum Stream : std::uint64_t { kCount, kSites, kVelocities, kKinds };

constexpr int kCandidatesPerSite = 10; // Mitchell's best candidate sampling
constexpr int kLloydRaster = 256;      // samples per side of the torus for the centroids
constexpr double kMinEdgeLength = 1.0; // shorter Voronoi edges are degenerate: not neighbors

// Share of continents, number of continental clusters, and chances of the water kinds.
constexpr double kMinLandShare = 0.35;
constexpr double kLandShareSpread = 0.10;
constexpr int kMinClusters = 2;
constexpr int kClusterSpread = 3; // 2 to 4 clusters
constexpr double kInlandHubChance = 0.5;
constexpr double kShelfChance = 0.4;

struct Offset {
    double x;
    double z;
};

using Neighbors = std::vector<std::vector<int>>;

double torusDistance2(double ax, double az, double bx, double bz, double period) {
    const double dx = wrapOffset(bx - ax, period);
    const double dz = wrapOffset(bz - az, period);
    return dx * dx + dz * dz;
}

// Best candidate sampling: each new site is the candidate farthest from the sites already
// placed, out of 10 per placed site. Evenly spread sites without any grid.
std::vector<Plate> placeSites(std::uint64_t seed, int count, double period) {
    Random random(seed);
    std::vector<Plate> plates;
    plates.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Plate best;
        double bestDistance = -1.0;
        for (int c = 0; c < kCandidatesPerSite * i + 1; ++c) {
            Plate candidate;
            candidate.x = random.unit() * period;
            candidate.z = random.unit() * period;
            double nearest = std::numeric_limits<double>::infinity();
            for (const Plate& p : plates) {
                nearest =
                    std::min(nearest, torusDistance2(p.x, p.z, candidate.x, candidate.z, period));
            }
            if (nearest > bestDistance) {
                best = candidate;
                bestDistance = nearest;
            }
        }
        plates.push_back(best);
    }
    return plates;
}

// One Lloyd iteration: every site moves to the centroid of its cell, measured on a raster of
// the torus with the offsets wrapped around the site.
void relax(std::vector<Plate>& plates, double period) {
    const std::size_t count = plates.size();
    std::vector<double> sumX(count, 0.0);
    std::vector<double> sumZ(count, 0.0);
    std::vector<int> samples(count, 0);
    const double step = period / kLloydRaster;
    for (int row = 0; row < kLloydRaster; ++row) {
        const double z = (row + 0.5) * step;
        for (int column = 0; column < kLloydRaster; ++column) {
            const double x = (column + 0.5) * step;
            const auto k = static_cast<std::size_t>(nearestSite(plates, x, z, period));
            sumX[k] += wrapOffset(x - plates[k].x, period);
            sumZ[k] += wrapOffset(z - plates[k].z, period);
            ++samples[k];
        }
    }
    for (std::size_t k = 0; k < count; ++k) {
        if (samples[k] > 0) {
            plates[k].x = wrapPosition(plates[k].x + sumX[k] / samples[k], period);
            plates[k].z = wrapPosition(plates[k].z + sumZ[k] / samples[k], period);
        }
    }
}

// Offsets from site i to the images of every site in the 3 x 3 tiles around its nearest
// image, except site i itself.
std::vector<Offset> imageOffsets(const std::vector<Plate>& plates, std::size_t i, double period) {
    std::vector<Offset> images;
    images.reserve(plates.size() * 9);
    for (std::size_t k = 0; k < plates.size(); ++k) {
        const double dx = wrapOffset(plates[k].x - plates[i].x, period);
        const double dz = wrapOffset(plates[k].z - plates[i].z, period);
        for (int tx = -1; tx <= 1; ++tx) {
            for (int tz = -1; tz <= 1; ++tz) {
                if (k != i || tx != 0 || tz != 0) {
                    images.push_back({dx + tx * period, dz + tz * period});
                }
            }
        }
    }
    return images;
}

// Length of the Voronoi edge between site i, at the origin, and the image `other` of another
// site: the part of their bisector closer to both than to any other image. Each image o
// keeps the half-plane |p|^2 <= |p - o|^2, i.e. 2 p.o <= |o|^2, which clips the bisector.
double edgeLength(const std::vector<Offset>& images, Offset other) {
    const double norm = std::hypot(other.x, other.z);
    const Offset mid{other.x / 2.0, other.z / 2.0};
    const Offset along{-other.z / norm, other.x / norm};
    double low = -std::numeric_limits<double>::infinity();
    double high = std::numeric_limits<double>::infinity();
    for (const Offset& o : images) {
        if (o.x == other.x && o.z == other.z) {
            continue;
        }
        const double slope = 2.0 * (along.x * o.x + along.z * o.z);
        const double room = o.x * o.x + o.z * o.z - 2.0 * (mid.x * o.x + mid.z * o.z);
        if (slope > 0.0) {
            high = std::min(high, room / slope);
        } else if (slope < 0.0) {
            low = std::max(low, room / slope);
        } else if (room < 0.0) {
            return 0.0;
        }
        if (high <= low) {
            return 0.0;
        }
    }
    return high - low;
}

// Exact neighbors on the torus: every pair whose Voronoi edge is longer than kMinEdgeLength,
// through the image of the second site with the longest edge. normals receives the unit
// vector from site a to that image, for each boundary.
std::vector<PlateBoundary> findBoundaries(const std::vector<Plate>& plates, double period,
                                          std::vector<Offset>& normals) {
    std::vector<PlateBoundary> boundaries;
    for (std::size_t i = 0; i < plates.size(); ++i) {
        const std::vector<Offset> images = imageOffsets(plates, i, period);
        for (std::size_t j = i + 1; j < plates.size(); ++j) {
            const double dx = wrapOffset(plates[j].x - plates[i].x, period);
            const double dz = wrapOffset(plates[j].z - plates[i].z, period);
            double length = 0.0;
            Offset normal{0.0, 0.0};
            for (int tx = -1; tx <= 1; ++tx) {
                for (int tz = -1; tz <= 1; ++tz) {
                    const Offset image{dx + tx * period, dz + tz * period};
                    const double edge = edgeLength(images, image);
                    if (edge > length) {
                        length = edge;
                        const double norm = std::hypot(image.x, image.z);
                        normal = {image.x / norm, image.z / norm};
                    }
                }
            }
            if (length > kMinEdgeLength) {
                PlateBoundary boundary;
                boundary.a = static_cast<int>(i);
                boundary.b = static_cast<int>(j);
                boundary.length = length;
                boundaries.push_back(boundary);
                normals.push_back(normal);
            }
        }
    }
    return boundaries;
}

// Random velocities (any direction, speed 0.5 to 1), then the kind of every boundary from the
// relative velocity of a toward b projected on the unit vector from a to b.
void moveAndClassify(std::vector<Plate>& plates, std::vector<PlateBoundary>& boundaries,
                     const std::vector<Offset>& normals, std::uint64_t seed) {
    Random random(seed);
    for (Plate& plate : plates) {
        const double angle = 2.0 * std::numbers::pi * random.unit();
        const double speed = 0.5 + 0.5 * random.unit();
        plate.velocityX = speed * std::cos(angle);
        plate.velocityZ = speed * std::sin(angle);
    }
    for (std::size_t k = 0; k < boundaries.size(); ++k) {
        PlateBoundary& boundary = boundaries[k];
        const Plate& a = plates[static_cast<std::size_t>(boundary.a)];
        const Plate& b = plates[static_cast<std::size_t>(boundary.b)];
        const double rx = a.velocityX - b.velocityX;
        const double rz = a.velocityZ - b.velocityZ;
        const double relative = std::hypot(rx, rz);
        const double toward = rx * normals[k].x + rz * normals[k].z;
        boundary.convergence = relative > 0.0 ? static_cast<float>(toward / relative) : 0.0f;
        if (boundary.convergence >= kCollisionThreshold) {
            boundary.kind = BoundaryKind::Collision;
        } else if (boundary.convergence <= -kCollisionThreshold) {
            boundary.kind = BoundaryKind::Divergence;
        } else {
            boundary.kind = BoundaryKind::Transform;
        }
    }
}

Neighbors neighborLists(std::size_t count, const std::vector<PlateBoundary>& boundaries) {
    Neighbors neighbors(count);
    for (const PlateBoundary& boundary : boundaries) {
        neighbors[static_cast<std::size_t>(boundary.a)].push_back(boundary.b);
        neighbors[static_cast<std::size_t>(boundary.b)].push_back(boundary.a);
    }
    return neighbors;
}

// Continental clusters growing over the neighbor graph: the cluster of each plate (-1 for
// water) and, per cluster, the plates to try next.
struct Growth {
    std::vector<int> owner;
    std::vector<std::deque<int>> fronts;
    int land = 0;
};

void claim(Growth& growth, const Neighbors& neighbors, int plate, int cluster, Random& random) {
    growth.owner[static_cast<std::size_t>(plate)] = cluster;
    ++growth.land;
    // Neighbors in a random order, so that the clusters grow in no fixed direction.
    std::vector<int> next = neighbors[static_cast<std::size_t>(plate)];
    for (std::size_t i = next.size(); i > 1; --i) {
        std::swap(next[i - 1], next[static_cast<std::size_t>(random.below(static_cast<int>(i)))]);
    }
    for (const int n : next) {
        if (growth.owner[static_cast<std::size_t>(n)] < 0) {
            growth.fronts[static_cast<std::size_t>(cluster)].push_back(n);
        }
    }
}

bool growOnce(Growth& growth, const Neighbors& neighbors, int cluster, Random& random) {
    std::deque<int>& front = growth.fronts[static_cast<std::size_t>(cluster)];
    while (!front.empty()) {
        const int plate = front.front();
        front.pop_front();
        if (growth.owner[static_cast<std::size_t>(plate)] < 0) {
            claim(growth, neighbors, plate, cluster, random);
            return true;
        }
    }
    return false;
}

// A water plate none of whose neighbors is land yet, to seed a new cluster; -1 if none.
int freeSeed(const Growth& growth, const Neighbors& neighbors, Random& random) {
    std::vector<int> free;
    for (std::size_t p = 0; p < neighbors.size(); ++p) {
        const bool isolated = std::none_of(neighbors[p].begin(), neighbors[p].end(), [&](int n) {
            return growth.owner[static_cast<std::size_t>(n)] >= 0;
        });
        if (growth.owner[p] < 0 && isolated) {
            free.push_back(static_cast<int>(p));
        }
    }
    return free.empty()
               ? -1
               : free[static_cast<std::size_t>(random.below(static_cast<int>(free.size())))];
}

// Continents: 35% to 45% of the plates, in 2 to 4 clusters of neighbors grown one plate per
// cluster in turn. The first cluster starts with a whole ring around its hub when the land
// budget allows it, so that the hub is enclosed. Returns that hub, or -1.
int growContinents(std::vector<Plate>& plates, const Neighbors& neighbors, Random& random) {
    const int count = static_cast<int>(plates.size());
    const double share = kMinLandShare + kLandShareSpread * random.unit();
    const int target = std::clamp(static_cast<int>(std::lround(count * share)), 2, count - 2);
    const int clusters = kMinClusters + random.below(kClusterSpread);
    Growth growth{std::vector<int>(plates.size(), -1),
                  std::vector<std::deque<int>>(static_cast<std::size_t>(clusters)), 0};
    const int hub = random.below(count);
    claim(growth, neighbors, hub, 0, random);
    const std::vector<int>& ring = neighbors[static_cast<std::size_t>(hub)];
    const bool enclosed = 1 + static_cast<int>(ring.size()) + clusters - 1 <= target;
    for (std::size_t i = 0; enclosed && i < ring.size(); ++i) {
        claim(growth, neighbors, ring[i], 0, random);
    }
    for (int c = 1; c < clusters; ++c) {
        const int seed = freeSeed(growth, neighbors, random);
        if (seed >= 0) {
            claim(growth, neighbors, seed, c, random);
        }
    }
    for (bool grew = true; grew && growth.land < target;) {
        grew = false;
        for (int c = 0; c < clusters && growth.land < target; ++c) {
            grew = growOnce(growth, neighbors, c, random) || grew;
        }
    }
    for (std::size_t p = 0; p < plates.size(); ++p) {
        plates[p].kind = growth.owner[p] >= 0 ? PlateKind::Continent : PlateKind::Ocean;
    }
    return enclosed ? hub : -1;
}

// Water plates enclosed by continents become inland seas, and so does the enclosed hub half
// of the time. Then 40% of the oceans next to a continent become continental shelves. At
// least one ocean always remains.
void assignWaters(std::vector<Plate>& plates, const Neighbors& neighbors, int hub, Random& random) {
    const auto continents = [&](std::size_t p) {
        return static_cast<int>(std::count_if(neighbors[p].begin(), neighbors[p].end(), [&](int n) {
            return plates[static_cast<std::size_t>(n)].kind == PlateKind::Continent;
        }));
    };
    if (hub >= 0 && random.unit() < kInlandHubChance) {
        plates[static_cast<std::size_t>(hub)].kind = PlateKind::InlandSea;
    }
    for (std::size_t p = 0; p < plates.size(); ++p) {
        const int around = static_cast<int>(neighbors[p].size());
        if (plates[p].kind == PlateKind::Ocean && around > 0 && continents(p) == around) {
            plates[p].kind = PlateKind::InlandSea;
        }
    }
    for (std::size_t p = 0; p < plates.size(); ++p) {
        if (plates[p].kind == PlateKind::Ocean && continents(p) > 0 &&
            random.unit() < kShelfChance) {
            plates[p].kind = PlateKind::ContinentalShelf;
        }
    }
    const auto isOcean = [](const Plate& p) { return p.kind == PlateKind::Ocean; };
    if (std::none_of(plates.begin(), plates.end(), isOcean)) {
        const auto water = std::find_if(plates.begin(), plates.end(), [](const Plate& p) {
            return p.kind != PlateKind::Continent;
        });
        water->kind = PlateKind::Ocean;
    }
}

} // namespace

int nearestSite(const std::vector<Plate>& plates, double x, double z, double period) {
    const double wx = wrapPosition(x, period);
    const double wz = wrapPosition(z, period);
    int nearest = 0;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < plates.size(); ++k) {
        const double dx = shortOffset(plates[k].x - wx, period);
        const double dz = shortOffset(plates[k].z - wz, period);
        const double d2 = dx * dx + dz * dz;
        if (d2 < best) {
            best = d2;
            nearest = static_cast<int>(k);
        }
    }
    return nearest;
}

Layout buildLayout(std::uint64_t seed, const VoronoiParams& params) {
    Random countRandom(noise::deriveSeed(seed, kCount));
    const int count = params.minPlates + countRandom.below(params.maxPlates - params.minPlates + 1);
    Layout layout;
    layout.planetSize = params.cellDiameter * std::sqrt(count * std::numbers::pi / 4.0);
    layout.plates = placeSites(noise::deriveSeed(seed, kSites), count, layout.planetSize);
    for (int i = 0; i < params.lloydIterations; ++i) {
        relax(layout.plates, layout.planetSize);
    }
    std::vector<Offset> normals;
    layout.boundaries = findBoundaries(layout.plates, layout.planetSize, normals);
    moveAndClassify(layout.plates, layout.boundaries, normals,
                    noise::deriveSeed(seed, kVelocities));
    const Neighbors neighbors = neighborLists(layout.plates.size(), layout.boundaries);
    Random kinds(noise::deriveSeed(seed, kKinds));
    const int hub = growContinents(layout.plates, neighbors, kinds);
    assignWaters(layout.plates, neighbors, hub, kinds);
    return layout;
}

} // namespace terrain::voronoi_layout
