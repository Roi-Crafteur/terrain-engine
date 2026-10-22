#include "geography_boundaries.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

namespace terrain::geography {

namespace {

// Wavelengths of the noises along the boundaries (fbm of 2 octaves, unrotated, but for the
// small meander of 1).
constexpr double kSpreadWavelength = 800'000.0;
constexpr double kRangesWavelength = 900'000.0;
constexpr double kMeanderLargeWavelength = 900'000.0;
constexpr double kMeanderSmallWavelength = 350'000.0;
constexpr double kExtentWavelength = 600'000.0;
constexpr double kEdgeWavelength = 300'000.0;

// The warp that bends the boundaries between 200 and 1,000 km, applied before that of the
// plates: up to kBendAmplitude meters, 2 octaves from kBendWavelength. Composed rather than
// added, it never folds the plane where the warp of the plates nearly does
// (geography_tectonics_test).
constexpr double kBendWavelength = 450'000.0;
constexpr double kBendAmplitude = 70'000.0;

// The half width of a belt is beltWidth times kWidthLow to kWidthHigh (log-uniform): chains of
// about 100 to 800 km; the foothills of a collision without a chain kFoothillsWidth times wider.
constexpr double kWidthLow = 0.25;
constexpr double kWidthHigh = 2.0;
constexpr double kFoothillsWidth = 2.5;

// The slow fields are computed on a grid kSlowCoarse times coarser than the map.
constexpr int kSlowCoarse = 2;

enum Slow : std::size_t { kOffsetX, kOffsetZ, kWidthNoise, kUpliftNoise, kRangesNoise };

double unit(std::uint64_t seed, std::uint64_t k) {
    return static_cast<double>(noise::deriveSeed(seed, k) >> 11) * 0x1.0p-53;
}

// The plate nearest to the warped point q (the lowest index on a tie).
int nearestPlate(const TectonicPlates& plates, TectonicPlates::Point q) {
    const WorldSize& world = plates.world();
    const double qx = noise::wrapCanonical(q.x, world.x);
    const double qz = noise::wrapCanonical(q.z, world.z);
    int best = 0;
    double bestDistance2 = 0.0;
    for (std::size_t k = 0; k < plates.plates().size(); ++k) {
        const Plate& plate = plates.plates()[k];
        const double dx = shortOffset(plate.x - qx, world.x);
        const double dz = shortOffset(plate.z - qz, world.z);
        const double d2 = dx * dx + dz * dz;
        if (k == 0 || d2 < bestDistance2) {
            best = static_cast<int>(k);
            bestDistance2 = d2;
        }
    }
    return best;
}

noise::FbmOctaves along(std::uint64_t stream, std::uint64_t s, int octaves, double wavelength,
                        const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(stream, s),
                             {.octaves = octaves,
                              .frequency = 1.0 / wavelength,
                              .lattices = noise::OctaveLattices::Unrotated},
                             world);
}

} // namespace

// The noises along the boundaries: the slow ones (read on the coarse grid), then those read at
// the nodes.
struct BoundaryFields::Noises {
    noise::FbmOctaves width;
    noise::FbmOctaves uplift;
    noise::FbmOctaves ranges;
    noise::FbmOctaves bendX;
    noise::FbmOctaves bendZ;
    noise::FbmOctaves meanderLarge;
    noise::FbmOctaves meanderSmall;
    noise::FbmOctaves extent;
    noise::FbmOctaves edge;
};

std::vector<double> landShares(std::size_t plates, const std::vector<int>& nearest,
                               const std::vector<std::uint8_t>& land) {
    std::vector<double> share(plates, 0.0);
    std::vector<double> all(plates, 0.0);
    for (std::size_t k = 0; k < nearest.size(); ++k) {
        const auto plate = static_cast<std::size_t>(nearest[k]);
        all[plate] += 1.0;
        share[plate] += land[k] != 0 ? 1.0 : 0.0;
    }
    for (std::size_t k = 0; k < plates; ++k) {
        share[k] = all[k] > 0.0 ? share[k] / all[k] : 0.0;
    }
    return share;
}

std::vector<Boundary> drawBoundaries(std::uint64_t seed, const GeographyParams& p,
                                     const TectonicPlates& plates,
                                     const std::vector<double>& landShare) {
    std::vector<Boundary> out;
    for (std::size_t k = 0; k < plates.boundaries().size(); ++k) {
        const PlateBoundary& from = plates.boundaries()[k];
        const std::uint64_t draws = noise::deriveSeed(seed, k);
        const double convergence = static_cast<double>(from.convergence);
        Boundary b;
        b.kind = convergence > kConvergent    ? BoundaryKind::Collision
                 : convergence < -kConvergent ? BoundaryKind::Divergence
                                              : BoundaryKind::Transform;
        b.strength = static_cast<float>(
            kWeakest + (1.0 - kWeakest) * std::clamp((std::abs(convergence) - kConvergent) /
                                                         (1.0 - kConvergent),
                                                     0.0, 1.0));
        const double logLow = dmath::log2(kWidthLow);
        b.halfWidth =
            p.beltWidth * dmath::exp2(logLow + (dmath::log2(kWidthHigh) - logLow) * unit(draws, 0));
        const auto a = static_cast<std::size_t>(from.a);
        const auto c = static_cast<std::size_t>(from.b);
        b.overriding = landShare[c] > landShare[a] ? from.b : from.a;
        // The weaker the collision, the likelier without a chain: chainlessShare of them on
        // average, the strongest almost never.
        const double weakness =
            1.0 - (static_cast<double>(b.strength) - kWeakest) / (1.0 - kWeakest);
        b.chainless = b.kind == BoundaryKind::Collision &&
                      unit(draws, 4) < 2.0 * static_cast<double>(p.chainlessShare) * weakness;
        if (b.chainless) {
            b.halfWidth *= kFoothillsWidth;
        }
        if (!b.chainless && from.length > p.plateauLength && unit(draws, 1) < p.plateauShare) {
            b.plateauWidth = p.plateauWidth * (0.6 + 0.8 * unit(draws, 2));
        }
        b.rift = convergence < -kRiftDivergence && unit(draws, 3) < p.riftShare;
        b.riftDepth = static_cast<float>(
            static_cast<double>(p.riftDepthLow) +
            static_cast<double>(p.riftDepthHigh - p.riftDepthLow) * unit(draws, 5));
        const double belt = b.halfWidth * (1.0 + kWidthSpread);
        const double meander = kMeanderLarge + kMeanderSmall;
        if (b.kind == BoundaryKind::Collision) {
            const double plateau = b.plateauWidth * (1.0 + kWidthSpread) * (1.0 + kPlateauExtent);
            b.reach = meander + std::max(belt * (1.0 + p.beltOffset) + plateau,
                                         belt * (1.0 + p.beltOffset) + p.trenchWidth);
        } else if (b.kind == BoundaryKind::Divergence) {
            b.reach = std::max(kShoulders * p.riftWidth, p.ridgeWidth);
        }
        out.push_back(b);
    }
    return out;
}

BoundaryFields::BoundaryFields(std::uint64_t stream, const TectonicPlates& plates,
                               const Grid& grid, const Crust& crust, int threads)
    : grid_(grid) {
    const WorldSize& world = plates.world();
    noises_ = std::make_shared<const Noises>(
        Noises{along(stream, 1, 2, kSpreadWavelength, world),
               along(stream, 2, 2, kSpreadWavelength, world),
               along(stream, 5, 2, kRangesWavelength, world),
               along(stream, 6, 2, kBendWavelength, world),
               along(stream, 7, 2, kBendWavelength, world),
               along(stream, 4, 2, kMeanderLargeWavelength, world),
               along(stream, 8, 1, kMeanderSmallWavelength, world),
               along(stream, 9, 2, kExtentWavelength, world),
               along(stream, 10, 2, kEdgeWavelength, world)});
    coarse_.nx = std::max(1, grid.nx / kSlowCoarse);
    coarse_.nz = std::max(1, grid.nz / kSlowCoarse);
    coarse_.sizeX = world.x / coarse_.nx;
    coarse_.sizeZ = world.z / coarse_.nz;
    slow_.resize(coarse_.nodes());
    nearest_.resize(coarse_.nodes());
    land_.resize(coarse_.nodes());
    planet::parallelRows(coarse_.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < coarse_.nx; ++i) {
                const std::size_t k = coarse_.index(i, j);
                const double x = coarse_.x(i);
                const double z = coarse_.z(j);
                const TectonicPlates::Point q = plates.warp(x, z);
                slow_[k] = {q.x - x, q.z - z, noises_->width.fbm2D(x, z),
                            noises_->uplift.fbm2D(x, z), noises_->ranges.fbm2D(x, z)};
                nearest_[k] = nearestPlate(plates, q);
                land_[k] = crust.land[grid.index(
                    static_cast<int>(std::floor(i * coarse_.sizeX / grid.sizeX + 0.5)),
                    static_cast<int>(std::floor(j * coarse_.sizeZ / grid.sizeZ + 0.5)))];
            }
        }
    });
}

NodeFrame BoundaryFields::at(int i, int j) const {
    const double x = grid_.x(i);
    const double z = grid_.z(j);
    const Noises& n = *noises_;
    const double bentX = x + kBendAmplitude * n.bendX.fbm2D(x, z);
    const double bentZ = z + kBendAmplitude * n.bendZ.fbm2D(x, z);
    const double u = (bentX - coarse_.x(0)) / coarse_.sizeX;
    const double v = (bentZ - coarse_.z(0)) / coarse_.sizeZ;
    const int i0 = static_cast<int>(std::floor(u));
    const int j0 = static_cast<int>(std::floor(v));
    const double tu = u - i0;
    const double tv = v - j0;
    const std::array<double, 4> weights = {(1.0 - tu) * (1.0 - tv), tu * (1.0 - tv),
                                           (1.0 - tu) * tv, tu * tv};
    std::array<double, 5> f{};
    for (int c = 0; c < 4; ++c) {
        const auto& values = slow_[coarse_.index(i0 + (c & 1), j0 + (c >> 1))];
        for (std::size_t m = 0; m < f.size(); ++m) {
            f[m] += weights[static_cast<std::size_t>(c)] * values[m];
        }
    }
    NodeFrame frame;
    frame.warped = {bentX + f[kOffsetX], bentZ + f[kOffsetZ]};
    frame.width = f[kWidthNoise];
    frame.uplift = f[kUpliftNoise];
    frame.ranges = f[kRangesNoise];
    frame.meander = kMeanderLarge * n.meanderLarge.fbm2D(x, z) +
                    kMeanderSmall * n.meanderSmall.fbm2D(x, z);
    frame.extent = n.extent.fbm2D(x, z);
    frame.edge = n.edge.fbm2D(x, z);
    return frame;
}

} // namespace terrain::geography
