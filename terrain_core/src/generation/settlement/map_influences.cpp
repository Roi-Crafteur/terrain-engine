#include "map_influences.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/preference.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::settlement {

double bump(double t) {
    if (std::abs(t) >= 1.0) {
        return 0.0;
    }
    const double u = 1.0 - t * t;
    return u * u;
}

PointField::PointField(double periodX, double periodZ, double maxReach)
    : periodX_(periodX), periodZ_(periodZ) {
    if (maxReach > 0.0) {
        countX_ = std::max(1, static_cast<int>(periodX_ / maxReach));
        countZ_ = std::max(1, static_cast<int>(periodZ_ / maxReach));
    }
}

int PointField::bucketX(double x) const {
    const int b = static_cast<int>((wrapCoordinate(x, periodX_) / periodX_ + 0.5) * countX_);
    return std::min(b, countX_ - 1);
}

int PointField::bucketZ(double z) const {
    const int b = static_cast<int>((wrapCoordinate(z, periodZ_) / periodZ_ + 0.5) * countZ_);
    return std::min(b, countZ_ - 1);
}

std::uint64_t PointField::key(int bx, int bz) const {
    const int wx = ((bx % countX_) + countX_) % countX_;
    const int wz = ((bz % countZ_) + countZ_) % countZ_;
    return static_cast<std::uint64_t>(wz) * static_cast<std::uint64_t>(countX_) +
           static_cast<std::uint64_t>(wx);
}

void PointField::add(double x, double z, double reach, double intensity) {
    buckets_[key(bucketX(x), bucketZ(z))].push_back({x, z, reach, intensity});
    ++count_;
}

double PointField::at(double x, double z) const {
    if (count_ == 0) {
        return 0.0;
    }
    const int bx = bucketX(x);
    const int bz = bucketZ(z);
    double best = 0.0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            // On a world of fewer than 3 buckets one is seen twice: the largest stays the same.
            const auto it = buckets_.find(key(bx + dx, bz + dz));
            if (it == buckets_.end()) {
                continue;
            }
            for (const Point& p : it->second) {
                const double d = torusDistance(x, z, p.x, p.z, periodX_, periodZ_);
                best = std::max(best, p.intensity * bump(d / p.reach));
            }
        }
    }
    return best;
}

namespace {

// The distance from (px, pz) to the segment from (ax, az) to (bx, bz).
double segmentDistance(double px, double pz, double ax, double az, double bx, double bz) {
    const double vx = bx - ax;
    const double vz = bz - az;
    const double length2 = vx * vx + vz * vz;
    double t = length2 > 0.0 ? ((px - ax) * vx + (pz - az) * vz) / length2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return dmath::hypot(px - (ax + t * vx), pz - (az + t * vz));
}

// Stamps the segment from point p to point q of a line on the land cells within its width.
void stampSegment(const PlanetGrid& grid, const TelluricLine& line, std::size_t p, std::size_t q,
                  planet::LandValues<std::uint8_t>& out) {
    const double reach = line.widthKm * 1000.0;
    const double half = 0.5 * grid.periodX();
    const double halfZ = 0.5 * grid.periodZ();
    const double lowX = std::min(line.x[p], line.x[q]) - reach;
    const double highX = std::max(line.x[p], line.x[q]) + reach;
    const double lowZ = std::min(line.z[p], line.z[q]) - reach;
    const double highZ = std::max(line.z[p], line.z[q]) + reach;
    const int i0 = static_cast<int>(std::floor((lowX + half) / grid.cellSize));
    const int i1 = static_cast<int>(std::floor((highX + half) / grid.cellSize));
    const int j0 = static_cast<int>(std::floor((lowZ + halfZ) / grid.cellSize));
    const int j1 = static_cast<int>(std::floor((highZ + halfZ) / grid.cellSize));
    const double intensity = std::max(line.intensity[p], line.intensity[q]);
    for (int j = j0; j <= j1; ++j) {
        const double cz = -halfZ + (j + 0.5) * grid.cellSize; // the image near the segment
        for (int i = i0; i <= i1; ++i) {
            const std::size_t cell = grid.index(grid.wrapColumn(i), grid.wrapRow(j));
            if (!out.index().land(cell)) {
                continue;
            }
            const double cx = -half + (i + 0.5) * grid.cellSize;
            const double d = segmentDistance(cx, cz, line.x[p], line.z[p], line.x[q], line.z[q]);
            const std::uint8_t v = quantize(intensity * bump(d / reach));
            std::uint8_t& slot = out.at(cell);
            slot = std::max(slot, v);
        }
    }
}

} // namespace

planet::LandValues<std::uint8_t>
stampLines(const PlanetGrid& grid, const std::shared_ptr<const planet::LandIndex>& land,
           const TelluricNetwork& network) {
    planet::LandValues<std::uint8_t> out(land, 0, 0);
    for (const TelluricLine& line : network.lines) {
        // Segments of about a quarter of the width: their boxes stay small.
        const auto stride = static_cast<std::size_t>(
            std::max(1.0, std::floor(line.widthKm / (4.0 * kLineStepKm))));
        const std::size_t last = line.x.size() - 1;
        for (std::size_t p = 0; p < last; p += stride) {
            stampSegment(grid, line, p, std::min(p + stride, last), out);
        }
    }
    return out;
}

} // namespace terrain::settlement
