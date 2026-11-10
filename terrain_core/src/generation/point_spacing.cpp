#include "point_spacing.hpp"

#include "terrain/det_math.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

double wrappedDistance(double ax, double az, double bx, double bz, double periodX,
                       double periodZ) {
    double dx = std::abs(ax - bx);
    double dz = std::abs(az - bz);
    dx = std::fmod(dx, periodX);
    dz = std::fmod(dz, periodZ);
    dx = std::min(dx, periodX - dx);
    dz = std::min(dz, periodZ - dz);
    return dmath::hypot(dx, dz);
}

PointSpacing::PointSpacing(double minDistance, double periodX, double periodZ)
    : minDistance_(minDistance), periodX_(periodX), periodZ_(periodZ) {
    if (minDistance_ > 0.0) {
        countX_ = std::max(1, static_cast<int>(periodX_ / minDistance_));
        countZ_ = std::max(1, static_cast<int>(periodZ_ / minDistance_));
    }
}

int PointSpacing::bucketX(double x) const {
    const int b = static_cast<int>((wrapCoordinate(x, periodX_) / periodX_ + 0.5) * countX_);
    return std::min(b, countX_ - 1);
}

int PointSpacing::bucketZ(double z) const {
    const int b = static_cast<int>((wrapCoordinate(z, periodZ_) / periodZ_ + 0.5) * countZ_);
    return std::min(b, countZ_ - 1);
}

std::uint64_t PointSpacing::key(int bx, int bz) const {
    const int wx = ((bx % countX_) + countX_) % countX_;
    const int wz = ((bz % countZ_) + countZ_) % countZ_;
    return static_cast<std::uint64_t>(wz) * static_cast<std::uint64_t>(countX_) +
           static_cast<std::uint64_t>(wx);
}

bool PointSpacing::farEnough(double x, double z) const {
    if (minDistance_ <= 0.0) {
        return true;
    }
    const int bx = bucketX(x);
    const int bz = bucketZ(z);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto it = buckets_.find(key(bx + dx, bz + dz));
            if (it == buckets_.end()) {
                continue;
            }
            for (const auto& [px, pz] : it->second) {
                if (wrappedDistance(x, z, px, pz, periodX_, periodZ_) < minDistance_) {
                    return false;
                }
            }
        }
    }
    return true;
}

void PointSpacing::add(double x, double z) {
    if (minDistance_ > 0.0) {
        buckets_[key(bucketX(x), bucketZ(z))].emplace_back(x, z);
    }
}

} // namespace terrain
