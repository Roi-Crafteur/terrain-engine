#pragma once

// Helpers of the cave district tests: districts over a synthetic ground (a function that gives
// the height and the environment at their sites), read once, with constant or varied climates
// and the counts of the types that they draw.

#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/noise.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace districts_test {

// The seed of the real-world checks: with it, every district type appears among the cells [-8, 8]^2
// around the cell (kWindowCellX, kWindowCellZ) of the default world.
constexpr std::uint64_t kSeed = 1337;

// A window of the default world at a southern mid-latitude (z near -9,000 km), whose climates run
// from glaciers on the mountains to lush caves: the origin, on the equator, is too warm for
// glaciers.
constexpr int kWindowCellX = 400;
constexpr int kWindowCellZ = -3000;

using TypeCounts = std::array<int, terrain::kCaveDistrictTypeCount>;

inline int countOf(const TypeCounts& counts, terrain::CaveDistrictType type) {
    return counts[static_cast<std::size_t>(type)];
}

// The ground height and the climate read at a district site.
struct Climate {
    float temperature = 12.0f;
    float humidity = 0.5f;
    float height = 50.0f;
};

inline terrain::DistrictSite constantSite(const Climate& climate) {
    return [climate](double, double, float& height, terrain::CaveEnvironment& environment) {
        height = climate.height;
        environment.temperature = climate.temperature;
        environment.humidity = climate.humidity;
    };
}

// Uniform number in [0, 1) from a position and a stream: a hash of the coordinates.
inline double hashUnit(double x, double z, std::uint64_t stream) {
    const std::uint64_t bits = terrain::noise::deriveSeed(
        terrain::noise::deriveSeed(stream, std::bit_cast<std::uint64_t>(x)),
        std::bit_cast<std::uint64_t>(z));
    return static_cast<double>(bits >> 40) / 16777216.0;
}

// A climate that varies from site to site: -15 to 30 degrees, every humidity, and ground from
// -100 to 600 m.
inline terrain::DistrictSite variedSite() {
    return [](double x, double z, float& height, terrain::CaveEnvironment& environment) {
        environment.temperature = static_cast<float>(-15.0 + 45.0 * hashUnit(x, z, 1));
        environment.humidity = static_cast<float>(hashUnit(x, z, 2));
        height = static_cast<float>(-100.0 + 700.0 * hashUnit(x, z, 3));
    };
}

// Districts of a world whose sites read `site`, each computed once.
class World {
  public:
    World(std::uint64_t seed, const terrain::CaveDistrictParams& params, terrain::DistrictSite site)
        : districts_(seed, params), site_(std::move(site)) {}

    [[nodiscard]] const terrain::CaveDistrict& at(int cellX, int cellZ) {
        const auto key = std::make_pair(cellX, cellZ);
        auto found = cache_.find(key);
        if (found == cache_.end()) {
            found = cache_.emplace(key, districts_.district(cellX, cellZ, site_)).first;
        }
        return found->second;
    }

    [[nodiscard]] terrain::DistrictWeights weights(double x, double z) {
        return districts_.weights(x, z,
                                  [this](int cellX, int cellZ) -> const terrain::CaveDistrict& {
                                      return at(cellX, cellZ);
                                  });
    }

    [[nodiscard]] terrain::CaveProfile profileAt(double x, double z) {
        return districts_.profile(weights(x, z));
    }

    [[nodiscard]] const terrain::CaveDistricts& districts() const { return districts_; }
    [[nodiscard]] const terrain::CaveDistrictParams& params() const { return districts_.params(); }

  private:
    terrain::CaveDistricts districts_;
    terrain::DistrictSite site_;
    std::map<std::pair<int, int>, terrain::CaveDistrict> cache_;
};

// Types of the districts of the cells [-half, half)^2.
inline TypeCounts countTypes(World& world, int half) {
    TypeCounts counts{};
    for (int cellZ = -half; cellZ < half; ++cellZ) {
        for (int cellX = -half; cellX < half; ++cellX) {
            ++counts[static_cast<std::size_t>(world.at(cellX, cellZ).type)];
        }
    }
    return counts;
}

inline TypeCounts countTypes(const terrain::DistrictSite& site, int half = 30,
                             const terrain::CaveDistrictParams& params = {}) {
    World world(kSeed, params, site);
    return countTypes(world, half);
}

} // namespace districts_test
