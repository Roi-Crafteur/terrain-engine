#pragma once

// Helpers of the aquifer tests: the carved density, the water density and the rock of a lattice
// of 1 m read from one sampler, and the places where the bisector of two neighbor cells crosses
// the line between their sites.

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"

#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace aquifers_test {

// Samples of a lattice, x varying fastest: rock is the terrain density, carved the density with
// the caves, water the density of the water of the aquifer of the column.
class Scan {
public:
    Scan(const terrain::CaveField& field, const terrain::CaveGround& ground,
         const terrain::CaveDetail& detail, const caves_test::Lattice& lattice)
        : lattice_(lattice) {
        const terrain::CaveBox box{static_cast<double>(lattice.x0),
                                   static_cast<double>(lattice.y0),
                                   static_cast<double>(lattice.z0),
                                   static_cast<double>(lattice.x0 + lattice.nx - 1),
                                   static_cast<double>(lattice.y0 + lattice.ny - 1),
                                   static_cast<double>(lattice.z0 + lattice.nz - 1)};
        terrain::CaveSampler sampler(field, ground, detail, box);
        for (int k = 0; k < lattice.nz; ++k) {
            for (int i = 0; i < lattice.nx; ++i) {
                const double x = lattice.x0 + i;
                const double z = lattice.z0 + k;
                columns_.push_back(sampler.column(x, z, ground.height(x, z)));
            }
        }
        for (int k = 0; k < lattice.nz; ++k) {
            for (int j = 0; j < lattice.ny; ++j) {
                for (int i = 0; i < lattice.nx; ++i) {
                    const terrain::CaveColumn& col = column(i, k);
                    const float solid = caves_test::terrainDensity(y(j), col.ground);
                    const float air = sampler.density(col, y(j), solid);
                    rock_.push_back(solid);
                    carved_.push_back(air);
                    water_.push_back(sampler.waterDensity(col, y(j), solid, air));
                }
            }
        }
    }

    [[nodiscard]] const caves_test::Lattice& lattice() const { return lattice_; }
    [[nodiscard]] double y(int j) const { return lattice_.y0 + j; }
    [[nodiscard]] const terrain::CaveColumn& column(int i, int k) const {
        return columns_[static_cast<std::size_t>(k) * static_cast<std::size_t>(lattice_.nx) +
                        static_cast<std::size_t>(i)];
    }
    [[nodiscard]] const std::vector<float>& rock() const { return rock_; }
    [[nodiscard]] const std::vector<float>& carved() const { return carved_; }
    [[nodiscard]] const std::vector<float>& water() const { return water_; }

private:
    caves_test::Lattice lattice_;
    std::vector<terrain::CaveColumn> columns_;
    std::vector<float> rock_;
    std::vector<float> carved_;
    std::vector<float> water_;
};

// Two neighbor cells, the first wet and the second dry or with a lower level, and the midpoint
// of their sites: on their bisector, nearest to both.
struct Bisector {
    terrain::AquiferCell home;
    terrain::AquiferCell other;
    double x = 0.0;
    double z = 0.0;
};

inline bool standsHigher(const terrain::AquiferCell& a, const terrain::AquiferCell& b) {
    return a.wet && (!b.wet || b.level < a.level);
}

// Square of count x count cells from (firstX, firstZ).
struct CellRange {
    int firstX;
    int firstZ;
    int count;
};

// Bisectors of the cells of the range (each cell with its neighbors to the east and to the
// south), over ground at least minHeight at their midpoint.
inline std::vector<Bisector> findBisectors(const terrain::CaveField& field,
                                           const terrain::CaveGround& ground,
                                           const CellRange& range, float minHeight) {
    terrain::AquiferCache cache(field, ground);
    std::vector<Bisector> found;
    for (int cellZ = range.firstZ; cellZ < range.firstZ + range.count; ++cellZ) {
        for (int cellX = range.firstX; cellX < range.firstX + range.count; ++cellX) {
            const terrain::AquiferCell a = cache.at(cellX, cellZ);
            const terrain::AquiferCell neighbors[2] = {cache.at(cellX + 1, cellZ),
                                                       cache.at(cellX, cellZ + 1)};
            for (const terrain::AquiferCell& b : neighbors) {
                if (!standsHigher(a, b) && !standsHigher(b, a)) {
                    continue;
                }
                const double x = (a.siteX + b.siteX) / 2.0;
                const double z = (a.siteZ + b.siteZ) / 2.0;
                const terrain::AquiferCell& nearest = cache.nearest(x, z);
                const bool between = (nearest.cellX == a.cellX && nearest.cellZ == a.cellZ) ||
                                     (nearest.cellX == b.cellX && nearest.cellZ == b.cellZ);
                if (between && ground.height(x, z) >= minHeight) {
                    found.push_back(standsHigher(a, b) ? Bisector{a, b, x, z}
                                                       : Bisector{b, a, x, z});
                }
            }
        }
    }
    return found;
}

// Lattice of 1 m around the midpoint of a bisector: half meters either side horizontally, from
// below meters under the level of the home cell to above meters over it.
inline caves_test::Lattice latticeAround(const Bisector& b, int half, int below, int above) {
    const int level = static_cast<int>(std::floor(b.home.level));
    return {static_cast<int>(std::lround(b.x)) - half,
            level - below,
            static_cast<int>(std::lround(b.z)) - half,
            2 * half + 1,
            below + above + 1,
            2 * half + 1};
}

// Water density the field gives over the carved densities (which may come from another scan).
inline std::vector<float> waterOver(const terrain::CaveField& field, const Scan& scan,
                                    const std::vector<float>& carved) {
    const caves_test::Lattice& l = scan.lattice();
    std::vector<float> water(l.count());
    for (int k = 0; k < l.nz; ++k) {
        for (int j = 0; j < l.ny; ++j) {
            for (int i = 0; i < l.nx; ++i) {
                const std::size_t n = l.index(i, j, k);
                water[n] =
                    field.waterDensity(scan.column(i, k), scan.y(j), scan.rock()[n], carved[n]);
            }
        }
    }
    return water;
}

struct LakeCounts {
    std::size_t water = 0;
    std::size_t inRock = 0;      // water within the skin of the rock (carved < 0)
    std::size_t outsideAir = 0;  // water with carved <= -skin
    std::size_t aboveLevel = 0;  // water at or over the level of its column (lake or river)
    std::size_t aboveGround = 0; // water within groundMargin of the ground, or over it
    std::size_t dryAir = 0;      // cave air under the level and the ground that is not water
    // Water with a horizontal neighbor of dry cave air, deeper under its level than the
    // largest slope of a river over a sample (CaveRiverParams::maxSlope per meter): the flat
    // lakes have none, their samples lying 0.5 m under the level at least; a river surface may
    // step down by its slope from a sample to the next.
    std::size_t walls = 0;
    std::size_t steps = 0;       // water against dry air within that step: a sloped surface
    std::size_t dryColumns = 0;  // samples of columns without lake nor river holding water

    void add(const LakeCounts& o) {
        water += o.water;
        inRock += o.inRock;
        outsideAir += o.outsideAir;
        aboveLevel += o.aboveLevel;
        aboveGround += o.aboveGround;
        dryAir += o.dryAir;
        walls += o.walls;
        steps += o.steps;
        dryColumns += o.dryColumns;
    }
};

// True if a horizontal neighbor of the sample (i, j, k) is neither water nor rock.
inline bool againstAir(const caves_test::Lattice& l, const std::vector<float>& water,
                       const std::vector<float>& carved, int i, int j, int k) {
    constexpr int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& step : kSteps) {
        const std::size_t n = l.index(i + step[0], j, k + step[1]);
        if (water[n] >= 0.0f && carved[n] >= 0.0f) {
            return true;
        }
    }
    return false;
}

// Properties of the water (density water over the carved densities) of the scan.
inline LakeCounts countLakes(const terrain::CaveField& field, const Scan& scan,
                             const std::vector<float>& carved, const std::vector<float>& water) {
    const terrain::AquiferParams& a = field.params().aquifers;
    const float maxStep = field.params().rivers.maxSlope; // over the 1 m between two samples
    const caves_test::Lattice& l = scan.lattice();
    LakeCounts counts;
    for (int k = 0; k < l.nz; ++k) {
        for (int j = 0; j < l.ny; ++j) {
            for (int i = 0; i < l.nx; ++i) {
                const std::size_t n = l.index(i, j, k);
                const terrain::CaveColumn& column = scan.column(i, k);
                const double level = static_cast<double>(field.waterLevelAt(column, scan.y(j)));
                const bool underLevel =
                    level > static_cast<double>(terrain::kDryLevel) && scan.y(j) < level;
                const bool underGround = scan.rock()[n] < -a.groundMargin;
                if (water[n] >= 0.0f) {
                    counts.dryAir += underLevel && underGround && carved[n] >= 0.0f ? 1 : 0;
                    continue;
                }
                ++counts.water;
                counts.inRock += carved[n] < 0.0f ? 1 : 0;
                counts.outsideAir += carved[n] > -a.skin ? 0 : 1;
                counts.aboveLevel += underLevel ? 0 : 1;
                counts.aboveGround += underGround ? 0 : 1;
                const bool dry = column.waterLevel <= terrain::kDryLevel &&
                                 column.riverLevel <= terrain::kDryLevel;
                counts.dryColumns += dry ? 1 : 0;
                const bool inside = i > 0 && i < l.nx - 1 && k > 0 && k < l.nz - 1;
                if (inside && againstAir(l, water, carved, i, j, k)) {
                    const bool step = level - scan.y(j) <= static_cast<double>(maxStep);
                    counts.steps += step ? 1 : 0;
                    counts.walls += step ? 0 : 1;
                }
            }
        }
    }
    return counts;
}

// Void of the barriers of the column at height y (positive in their rock): the largest
// min(width, top - y), or -1e9 without barrier.
inline float slabAt(const terrain::CaveColumn& column, double y) {
    float slab = -1.0e9f;
    for (int n = 0; n < column.barrierCount; ++n) {
        const terrain::AquiferBarrier& barrier = column.barriers[static_cast<std::size_t>(n)];
        slab = std::max(slab, std::min(barrier.width,
                                       static_cast<float>(static_cast<double>(barrier.top) - y)));
    }
    return slab;
}

struct BarrierCounts {
    std::size_t closed = 0;  // air without the aquifers, rock with them
    std::size_t strayed = 0; // closed, outside the slabs
    std::size_t open = 0;    // inside a slab, deep enough, but air
    std::size_t changed = 0; // samples of columns without barrier whose density changed
    std::size_t added = 0;   // samples whose density rose with the aquifers: rock removed

    void add(const BarrierCounts& o) {
        closed += o.closed;
        strayed += o.strayed;
        open += o.open;
        changed += o.changed;
        added += o.added;
    }
};

// Compares the scan of a detail with the aquifers to the one of the same lattice without them.
inline BarrierCounts countBarriers(const terrain::CaveField& field, const Scan& with,
                                   const Scan& without) {
    const caves_test::Lattice& l = with.lattice();
    const float blend = field.params().blend;
    BarrierCounts counts;
    for (int k = 0; k < l.nz; ++k) {
        for (int j = 0; j < l.ny; ++j) {
            for (int i = 0; i < l.nx; ++i) {
                const std::size_t n = l.index(i, j, k);
                const terrain::CaveColumn& column = with.column(i, k);
                const float solid = with.rock()[n];
                const float after = with.carved()[n];
                const float before = without.carved()[n];
                const float slab = slabAt(column, with.y(j));
                counts.added += after > before + 1.0e-4f ? 1 : 0;
                counts.changed += column.barrierCount == 0 && after != before ? 1 : 0;
                counts.open += slab > 0.0f && solid < -(blend + 1.0f) && after >= 0.0f ? 1 : 0;
                if (caves_test::carvedAir(solid, before) && after < 0.0f) {
                    ++counts.closed;
                    counts.strayed += slab > 0.0f ? 0 : 1;
                }
            }
        }
    }
    return counts;
}

// Detail of the caves without the barriers and the water.
inline terrain::CaveDetail withoutAquifers() {
    terrain::CaveDetail detail;
    detail.components =
        terrain::kAllCaveComponents & ~static_cast<unsigned>(terrain::kCaveAquifers);
    return detail;
}

// Wet and frozen cells among the cells [-half, half) on both axes.
struct CellShare {
    int cells = 0;
    int wet = 0;
    int frozen = 0;
};

inline CellShare shareOf(const terrain::CaveField& field, const terrain::CaveGround& ground,
                         int half) {
    CellShare share;
    for (int cellZ = -half; cellZ < half; ++cellZ) {
        for (int cellX = -half; cellX < half; ++cellX) {
            const terrain::AquiferCell cell = field.aquifer(cellX, cellZ, ground);
            ++share.cells;
            share.wet += cell.wet ? 1 : 0;
            share.frozen += cell.frozen ? 1 : 0;
        }
    }
    return share;
}

// Flat ground over a grassland of the given temperature and humidity.
inline terrain::CaveGround climateGround(float height, float temperature, float humidity) {
    return caves_test::withEnvironment(
        caves_test::flatGround(height),
        caves_test::environmentOf(terrain::Biome::Grassland, temperature, humidity));
}

inline double largestRadius(const terrain::CaveEntrance& e) {
    return static_cast<double>(std::max(e.tubeRadius, e.chamberRadius));
}

struct EntranceWater {
    std::size_t inside = 0; // samples of the tube and the chamber (entrance void < 0)
    std::size_t wet = 0;    // of them with water
};

// Water in the tube and the chamber of an entrance, on a lattice of 1 m.
inline EntranceWater waterInEntrance(const terrain::CaveField& field,
                                     const terrain::CaveGround& ground,
                                     const terrain::CaveEntrance& e) {
    const double r = largestRadius(e) + 1.0;
    const terrain::CaveBox box{
        std::min(e.topX, e.bottomX) - r, e.bottomY - r, std::min(e.topZ, e.bottomZ) - r,
        std::max(e.topX, e.bottomX) + r, e.topY + r,    std::max(e.topZ, e.bottomZ) + r};
    terrain::CaveSampler sampler(field, ground, terrain::CaveDetail{}, box);
    EntranceWater result;
    for (double z = std::ceil(box.minZ); z <= box.maxZ; z += 1.0) {
        for (double x = std::ceil(box.minX); x <= box.maxX; x += 1.0) {
            std::optional<terrain::CaveColumn> column;
            for (double y = std::ceil(box.minY); y <= box.maxY; y += 1.0) {
                if (terrain::entranceVoidAt(e, x, y, z) >= 0.0f) {
                    continue;
                }
                if (!column) {
                    column = sampler.column(x, z, ground.height(x, z));
                }
                const float solid = caves_test::terrainDensity(y, column->ground);
                const float carved = sampler.density(*column, y, solid);
                ++result.inside;
                result.wet += sampler.waterDensity(*column, y, solid, carved) < 0.0f ? 1 : 0;
            }
        }
    }
    return result;
}

// Lowest clearance of the tube and the chamber over the water and barrier tops around them,
// probed every 2 m along the axis: the lowest point of the part (axis point minus radius) minus
// the highest level within reach of the barriers.
inline double clearanceOver(const terrain::CaveEntrance& e, terrain::AquiferCache& cache) {
    const double length = std::hypot(e.bottomX - e.topX, e.bottomY - e.topY, e.bottomZ - e.topZ);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / 2.0)));
    double lowest = 1.0e9;
    for (int step = 0; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        const double radius = step == steps ? e.chamberRadius : e.tubeRadius;
        const double x = e.topX + t * (e.bottomX - e.topX);
        const double z = e.topZ + t * (e.bottomZ - e.topZ);
        const double y = e.topY + t * (e.bottomY - e.topY);
        const double level = static_cast<double>(cache.highestLevelNear(x, z, radius));
        lowest = std::min(lowest, y - radius - level);
    }
    return lowest;
}

} // namespace aquifers_test
