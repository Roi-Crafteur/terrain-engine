#pragma once

// The worlds of the tests of the seas of dunes (agent 34): the default world with a coarse planet
// level (cells of 65,536 m, built in a few seconds) for the dunes at their real scale and far from
// the origin, and the small world (golden::kSmallPlanet) with its planet level and ergs scaled to
// it for the mask.

#include "terrain/generation/ergs.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_build.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include "planet_golden_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace dunes_test {

inline constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
inline constexpr double kDegrees = 57.29577951308232;

inline int threads() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

// The default world, its relief and a coarse planet level.
struct RealWorld {
    terrain::Heightfield relief{kSeed};
    std::shared_ptr<const terrain::PlanetLevel> planet = [this] {
        terrain::PlanetParams p;
        p.threads = threads();
        p.cellSize = 65536.0;
        return std::make_shared<const terrain::PlanetLevel>(terrain::planet::buildPlanet(relief, p), p);
    }();
    terrain::Ergs ergs{kSeed, *planet, {}, terrain::ErgParams{}};
};

inline const RealWorld& real() {
    static const RealWorld world;
    return world;
}

// The ergs scaled to the small world (2,097 x 1,049 km): its continents are small.
inline terrain::ErgParams smallErgParams() {
    terrain::ErgParams p;
    p.coastNear = 20'000.0;
    p.coastFar = 80'000.0;
    p.supplyWavelength = 120'000.0;
    p.edgeWavelength = 20'000.0;
    return p;
}

struct SmallWorld {
    terrain::Heightfield relief{kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet};
    std::shared_ptr<const terrain::PlanetLevel> planet =
        std::make_shared<const terrain::PlanetLevel>(
            terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads())),
            golden::smallPlanetParams(threads()));
    terrain::Ergs ergs{kSeed, *planet, relief.depressions(), smallErgParams()};
};

inline const SmallWorld& small() {
    static const SmallWorld world;
    return world;
}

// An erg point of a kind, wherever: a full mask, this sand and variability, the wind of the place.
inline terrain::ErgPoint forced(const terrain::Ergs& ergs, double x, double z, float sand,
                                float variability) {
    terrain::ErgPoint e;
    e.mask = 1.0f;
    e.sand = sand;
    e.variability = variability;
    const std::array<double, 2> w = ergs.windAt(x, z);
    e.windX = w[0];
    e.windZ = w[1];
    return e;
}

// The dunes of a forced kind at (x, z), wrapped onto the world.
inline double forcedDunes(const terrain::Ergs& ergs, double x, double z, float sand,
                          float variability) {
    const terrain::PlanetGrid& g = ergs.grid();
    x = terrain::wrapCoordinate(x, g.periodX());
    z = terrain::wrapCoordinate(z, g.periodZ());
    return static_cast<double>(ergs.dunesOf(x, z, forced(ergs, x, z, sand, variability)));
}

// The slope at (x, z), degrees, from differences over 0.5 m.
inline double slopeAt(const terrain::Ergs& ergs, double x, double z, float sand, float variability) {
    const double h = forcedDunes(ergs, x, z, sand, variability);
    const double dx = (forcedDunes(ergs, x + 0.5, z, sand, variability) - h) / 0.5;
    const double dz = (forcedDunes(ergs, x, z + 0.5, sand, variability) - h) / 0.5;
    return std::atan(std::sqrt(dx * dx + dz * dz)) * kDegrees;
}

// The crests met along a line from (x, z) in the direction (ux, uz), every step meters over length:
// the distances along the line of the local maxima above floor meters.
inline std::vector<double> crestsAlong(const terrain::Ergs& ergs, double x, double z, double ux,
                                       double uz, double length, double step, float sand,
                                       float variability, double floor) {
    std::vector<double> h;
    for (double s = 0.0; s <= length; s += step) {
        h.push_back(forcedDunes(ergs, x + s * ux, z + s * uz, sand, variability));
    }
    std::vector<double> crests;
    for (std::size_t i = 1; i + 1 < h.size(); ++i) {
        if (h[i] > floor && h[i] > h[i - 1] && h[i] >= h[i + 1]) {
            crests.push_back(static_cast<double>(i) * step);
        }
    }
    return crests;
}

inline std::vector<double> spacings(const std::vector<double>& crests) {
    std::vector<double> out;
    for (std::size_t i = 1; i < crests.size(); ++i) {
        out.push_back(crests[i] - crests[i - 1]);
    }
    return out;
}

inline double quantile(std::vector<double> v, double q) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(k, v.size() - 1)];
}

} // namespace dunes_test
