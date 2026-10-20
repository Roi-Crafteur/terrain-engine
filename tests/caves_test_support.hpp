#pragma once

// Helpers shared by the cave tests: grounds for the caves to read (flat, the heightfield, with
// a straight river), a deterministic random stream, and the test for air carved out of rock.

#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace caves_test {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// Deterministic pseudo-random numbers in [0, 1): the stream is a seed, each call a counter.
class Rng {
public:
    explicit Rng(std::uint64_t stream) : stream_(stream) {}

    double next() {
        const std::uint64_t bits = terrain::noise::deriveSeed(stream_, counter_++);
        return static_cast<double>(bits >> 40) / 16777216.0;
    }
    double range(double low, double high) { return low + (high - low) * next(); }

private:
    std::uint64_t stream_;
    std::uint64_t counter_ = 0;
};

// Ground at the same height everywhere, without river.
inline terrain::CaveGround flatGround(float height) {
    terrain::CaveGround ground;
    ground.height = [height](double, double) { return height; };
    return ground;
}

// The ground of the heightfield, without river. The field must outlive the ground.
inline terrain::CaveGround heightfieldGround(const terrain::Heightfield& field) {
    terrain::CaveGround ground;
    ground.height = [&field](double x, double z) { return field.heightAt(x, z); };
    return ground;
}

// The same ground with a river along the line x = riverX: its distance is |x - riverX|.
inline terrain::CaveGround withStraightRiver(terrain::CaveGround ground, double riverX) {
    ground.riverDistance = [riverX](double x, double) {
        return static_cast<float>(std::abs(x - riverX));
    };
    return ground;
}

// Terrain density of a point at height y of a column whose ground is ground.
inline float terrainDensity(double y, float ground) {
    return static_cast<float>(y - static_cast<double>(ground));
}

// Air that the caves carved out of rock: solid in the terrain, open once carved.
inline bool carvedAir(float terrain, float carved) {
    return terrain < 0.0f && carved >= 0.0f;
}

// Smallest box holding the points added to it.
class BoxBuilder {
public:
    void add(double x, double y, double z) {
        if (!started_) {
            box_ = {x, y, z, x, y, z};
            started_ = true;
            return;
        }
        box_.minX = std::min(box_.minX, x);
        box_.minY = std::min(box_.minY, y);
        box_.minZ = std::min(box_.minZ, z);
        box_.maxX = std::max(box_.maxX, x);
        box_.maxY = std::max(box_.maxY, y);
        box_.maxZ = std::max(box_.maxZ, z);
    }
    // The box grown by margin meters on every side.
    [[nodiscard]] terrain::CaveBox box(double margin = 0.0) const {
        return {box_.minX - margin, box_.minY - margin, box_.minZ - margin,
                box_.maxX + margin, box_.maxY + margin, box_.maxZ + margin};
    }

private:
    terrain::CaveBox box_;
    bool started_ = false;
};

// A point of the world and the ground height of its column.
struct Point {
    double x;
    double y;
    double z;
    float ground;
};

// Square of half-size `half` around (x, z).
struct Area {
    double x;
    double z;
    double half;
};

// Area of the given half-side centered on the opening of the first entrance found within 2 km
// of (x, z), or on (x, z) if there is none.
inline Area areaAroundEntrance(const terrain::CaveField& field, const terrain::CaveGround& ground,
                               double x, double z, double half) {
    constexpr double kSearch = 2048.0;
    const std::vector<terrain::CaveEntrance> entrances =
        field.entrancesNear(x - kSearch, z - kSearch, x + kSearch, z + kSearch, ground);
    if (entrances.empty()) {
        return {x, z, half};
    }
    return {entrances.front().topX, entrances.front().topZ, half};
}

// Random points of the area, at a depth in [minDepth, maxDepth] below the ground.
inline std::vector<Point> randomPoints(const terrain::CaveGround& ground, Rng& rng, int count,
                                       const Area& area, double minDepth, double maxDepth) {
    std::vector<Point> points;
    for (int k = 0; k < count; ++k) {
        const double x = area.x + rng.range(-area.half, area.half);
        const double z = area.z + rng.range(-area.half, area.half);
        const float height = ground.height(x, z);
        const double y = static_cast<double>(height) - rng.range(minDepth, maxDepth);
        points.push_back({x, y, z, height});
    }
    return points;
}

// Random point around the axis of an entrance: a point of the axis moved by up to `spread`
// meters along each of the three axes.
inline Point pointNearEntrance(const terrain::CaveGround& ground, Rng& rng,
                               const terrain::CaveEntrance& entrance, double spread) {
    const double t = rng.next();
    const double x = entrance.topX + t * (entrance.bottomX - entrance.topX) +
                     rng.range(-spread, spread);
    const double y = entrance.topY + t * (entrance.bottomY - entrance.topY) +
                     rng.range(-spread, spread);
    const double z = entrance.topZ + t * (entrance.bottomZ - entrance.topZ) +
                     rng.range(-spread, spread);
    return {x, y, z, ground.height(x, z)};
}

inline terrain::CaveBox boxOf(const std::vector<Point>& points) {
    BoxBuilder builder;
    for (const Point& p : points) {
        builder.add(p.x, p.y, p.z);
    }
    return builder.box(1.0);
}

// Carved density at a point from a sampler of the box that holds it.
inline float carvedAt(terrain::CaveSampler& sampler, const Point& p) {
    const terrain::CaveColumn column = sampler.column(p.x, p.z, p.ground);
    return sampler.density(column, p.y, terrainDensity(p.y, p.ground));
}

// Columns of a grid: from (minX, minZ) over sizeX and sizeZ every `step` meters, and in each
// column the depths from minDepth to maxDepth below the ground, every meter.
struct Window {
    double minX;
    double minZ;
    double sizeX;
    double sizeZ;
    double step;
    int minDepth;
    int maxDepth;
};

// Calls visit(sampler, column, y, terrain, carved) for every sample of the window, from one
// sampler over the whole window.
template <typename Visit>
void scanColumns(const terrain::CaveField& field, const terrain::CaveGround& ground,
                 const Window& w, Visit visit) {
    struct Column {
        double x;
        double z;
        float ground;
    };
    std::vector<Column> columns;
    BoxBuilder builder;
    const int countX = static_cast<int>(w.sizeX / w.step) + 1;
    const int countZ = static_cast<int>(w.sizeZ / w.step) + 1;
    for (int j = 0; j < countZ; ++j) {
        for (int i = 0; i < countX; ++i) {
            const double x = w.minX + i * w.step;
            const double z = w.minZ + j * w.step;
            const float height = ground.height(x, z);
            columns.push_back({x, z, height});
            builder.add(x, static_cast<double>(height) - w.maxDepth, z);
            builder.add(x, static_cast<double>(height) - w.minDepth, z);
        }
    }
    terrain::CaveSampler sampler(field, ground, terrain::CaveDetail{}, builder.box(1.0));
    for (const Column& c : columns) {
        const terrain::CaveColumn column = sampler.column(c.x, c.z, c.ground);
        for (int depth = w.minDepth; depth <= w.maxDepth; ++depth) {
            const double y = static_cast<double>(c.ground) - depth;
            const float rock = terrainDensity(y, c.ground);
            visit(sampler, column, y, rock, sampler.density(column, y, rock));
        }
    }
}

// A point on the axis of a ravine, inside a ravine area, over ground of the given height: a
// coarse search of the strong areas, then a fine search of the nearest zero line of the axis.
inline bool findRavine(const terrain::CaveField& field, const terrain::CaveGround& ground,
                       float height, double& siteX, double& siteZ) {
    constexpr int kCoarse = 64;
    constexpr double kFine = 2.0;
    constexpr int kReach = 4096;
    for (int j = -kReach; j <= kReach; j += kCoarse) {
        for (int i = -kReach; i <= kReach; i += kCoarse) {
            if (field.column(i, j, height, ground, {}).ravineStrength < 0.7f) {
                continue;
            }
            for (double z = j - 128.0; z <= j + 128.0; z += kFine) {
                for (double x = i - 128.0; x <= i + 128.0; x += kFine) {
                    const terrain::CaveColumn column = field.column(x, z, height, ground, {});
                    if (column.ravineStrength >= 0.6f && column.ravineDistance <= 1.0f) {
                        siteX = x;
                        siteZ = z;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Lattice of the integer positions of a box, 1 m apart: (x0 + i, y0 + j, z0 + k).
struct Lattice {
    int x0;
    int y0;
    int z0;
    int nx;
    int ny;
    int nz;

    [[nodiscard]] std::size_t index(int i, int j, int k) const {
        return (static_cast<std::size_t>(k) * static_cast<std::size_t>(ny) +
                static_cast<std::size_t>(j)) * static_cast<std::size_t>(nx) +
               static_cast<std::size_t>(i);
    }
    [[nodiscard]] std::size_t count() const {
        return static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) *
               static_cast<std::size_t>(nz);
    }
};

// Flood fill, through the samples of carved density >= 0, from the sky of a lattice: the
// samples at least 1 m over the ground of their column.
class SkyFill {
public:
    SkyFill(const terrain::CaveField& field, const terrain::CaveGround& ground,
            const Lattice& lattice)
        : lattice_(lattice),
          sampler_(field, ground, terrain::CaveDetail{},
                   {static_cast<double>(lattice_.x0), static_cast<double>(lattice_.y0),
                    static_cast<double>(lattice_.z0),
                    static_cast<double>(lattice_.x0 + lattice_.nx - 1),
                    static_cast<double>(lattice_.y0 + lattice_.ny - 1),
                    static_cast<double>(lattice_.z0 + lattice_.nz - 1)}),
          state_(lattice_.count(), kUnknown) {
        for (int k = 0; k < lattice_.nz; ++k) {
            for (int i = 0; i < lattice_.nx; ++i) {
                const double x = lattice_.x0 + i;
                const double z = lattice_.z0 + k;
                grounds_.push_back(ground.height(x, z));
                columns_.push_back(sampler_.column(x, z, grounds_.back()));
            }
        }
    }

    // Marks the samples reached from the sky.
    void run() {
        std::vector<std::size_t> stack;
        for (int k = 0; k < lattice_.nz; ++k) {
            for (int j = 0; j < lattice_.ny; ++j) {
                for (int i = 0; i < lattice_.nx; ++i) {
                    if (static_cast<double>(lattice_.y0 + j) >= grounds_[column(i, k)] + 1.0) {
                        state_[lattice_.index(i, j, k)] = kReached;
                        stack.push_back(lattice_.index(i, j, k));
                    }
                }
            }
        }
        constexpr int kSteps[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1},
                                      {0, 0, -1}};
        while (!stack.empty()) {
            const std::size_t at = stack.back();
            stack.pop_back();
            const auto nx = static_cast<std::size_t>(lattice_.nx);
            const auto ny = static_cast<std::size_t>(lattice_.ny);
            const int i = static_cast<int>(at % nx);
            const int j = static_cast<int>(at / nx % ny);
            const int k = static_cast<int>(at / nx / ny);
            for (const auto& step : kSteps) {
                const int a = i + step[0];
                const int b = j + step[1];
                const int c = k + step[2];
                if (a >= 0 && b >= 0 && c >= 0 && a < lattice_.nx && b < lattice_.ny &&
                    c < lattice_.nz && state_[lattice_.index(a, b, c)] != kReached &&
                    isAir(a, b, c)) {
                    state_[lattice_.index(a, b, c)] = kReached;
                    stack.push_back(lattice_.index(a, b, c));
                }
            }
        }
    }

    // True if the sample nearest to the point was reached.
    [[nodiscard]] bool reached(double x, double y, double z) const {
        const int i = static_cast<int>(std::lround(x)) - lattice_.x0;
        const int j = static_cast<int>(std::lround(y)) - lattice_.y0;
        const int k = static_cast<int>(std::lround(z)) - lattice_.z0;
        return state_[lattice_.index(i, j, k)] == kReached;
    }

    // Greatest depth below the ground of its column of a reached sample, and the number of
    // reached samples under the ground.
    void undergroundExtent(double& deepest, std::size_t& underground) const {
        deepest = 0.0;
        underground = 0;
        for (int k = 0; k < lattice_.nz; ++k) {
            for (int j = 0; j < lattice_.ny; ++j) {
                for (int i = 0; i < lattice_.nx; ++i) {
                    const double depth = static_cast<double>(grounds_[column(i, k)]) -
                                         static_cast<double>(lattice_.y0 + j);
                    if (state_[lattice_.index(i, j, k)] == kReached && depth > 0.0) {
                        deepest = std::max(deepest, depth);
                        ++underground;
                    }
                }
            }
        }
    }

    [[nodiscard]] std::size_t samples() const { return state_.size(); }

private:
    enum State : std::uint8_t { kUnknown, kSolid, kAir, kReached };

    [[nodiscard]] std::size_t column(int i, int k) const {
        return static_cast<std::size_t>(k) * static_cast<std::size_t>(lattice_.nx) +
               static_cast<std::size_t>(i);
    }

    // True if the carved density at the sample is >= 0, evaluated once.
    bool isAir(int i, int j, int k) {
        std::uint8_t& state = state_[lattice_.index(i, j, k)];
        if (state == kUnknown) {
            const double y = lattice_.y0 + j;
            const float rock = terrainDensity(y, grounds_[column(i, k)]);
            state = sampler_.density(columns_[column(i, k)], y, rock) >= 0.0f ? kAir : kSolid;
        }
        return state != kSolid;
    }

    Lattice lattice_;
    terrain::CaveSampler sampler_;
    std::vector<std::uint8_t> state_;
    std::vector<float> grounds_;
    std::vector<terrain::CaveColumn> columns_;
};

} // namespace caves_test
