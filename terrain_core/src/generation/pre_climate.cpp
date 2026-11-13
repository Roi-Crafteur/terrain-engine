#include "terrain/generation/pre_climate.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <deque>
#include <mutex>
#include <utility>

namespace terrain {

namespace {

// The cells of a small world: at most a kRowsLeast-th of its side along z.
constexpr double kRowsLeast = 128.0;

// The planet level reads the floor of its cells (the lowest of 9 points): in the mountains, the
// floors of their valleys. The air flows here over the regional elevation, plus kMountainFloor of
// the height the mountains may reach there (GeographyParams::mountainHeight as the intensity of
// the relief passes kHeightLow to kHeightHigh, under their mask from kMaskLow to kMaskHigh, as
// the Heightfield draws them).
constexpr float kMountainFloor = 0.25f;
constexpr float kHeightLow = 0.1f;
constexpr float kHeightHigh = 0.7f;
constexpr float kMaskLow = 0.12f;
constexpr float kMaskHigh = 0.5f;

// The smallest ocean, square meters (PlanetParams::minOceanArea).
constexpr double kMinOceanArea = 1.0e10;

// The humidity reads the potential evapotranspiration as kLeastPet mm/yr at least, as the planet
// level does (PlanetLevel::humidityAt).
constexpr double kLeastPet = 300.0;

void addBits(std::uint64_t& hash, std::uint32_t bits) {
    for (int k = 0; k < 4; ++k) {
        hash ^= (bits >> (8 * k)) & 0xFFU;
        hash *= 1099511628211ULL;
    }
}

} // namespace

double PreClimate::cellSizeFor(const WorldSize& world) {
    double cell = kPreClimateCell;
    while (cell > 1.0 && cell > world.z / kRowsLeast) {
        cell *= 0.5;
    }
    return cell;
}

PreClimate::PreClimate(const Geography& geography, const PlanetClimateParams& params,
                       int threads)
    : params_(params), grid_(makePlanetGrid(geography.world(), cellSizeFor(geography.world()))) {
    const std::size_t cells = grid_.cells();
    height_.assign(cells, 0.0f);
    std::vector<std::uint8_t> sea(cells, 0);
    const GeographyParams& g = geography.params();
    const int workers = planet::resolveThreads(threads);
    planet::parallelRows(grid_.nz, workers, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid_.nx; ++i) {
                const std::size_t c = grid_.index(i, j);
                const GeographySample s = geography.sampleAt(grid_.centerX(i), grid_.centerZ(j));
                if (s.coast < 0.0) {
                    sea[c] = 1;
                    height_[c] = std::min(s.sea, 0.0f);
                    continue;
                }
                const float mountains =
                    g.mountainHeight * noise::smoothstep(kHeightLow, kHeightHigh, s.relief) *
                    noise::smoothstep(kMaskLow, kMaskHigh, s.relief);
                height_[c] = std::max(s.land, 1.0f) + kMountainFloor * mountains;
            }
        }
    });
    ocean_ = planet::markOcean(grid_, height_, sea, kMinOceanArea);
    const planet::PlanetClimate climate =
        planet::computeClimate(grid_, height_, ocean_, params_, workers);
    precipitation_ = climate.precipitation;
    temperature_.assign(cells, 0.0f);
    humidity_.assign(cells, 0.0f);
    for (std::size_t c = 0; c < cells; ++c) {
        const float row = climate.rowTemperature[static_cast<std::size_t>(grid_.row(c))];
        temperature_[c] = planet::cellTemperature(row, ocean_[c] != 0 ? 0.0f : height_[c], params_);
        const double p = precipitation_[c];
        const double pet = std::max(
            planet::potentialEvapotranspiration(temperature_[c], params_), kLeastPet);
        humidity_[c] = static_cast<float>(p / (p + pet));
        ocean_[c] = ocean_[c] != planet::kLand ? std::uint8_t{1} : std::uint8_t{0};
    }
}

namespace {

float bilinear(const PlanetGrid& grid, const std::vector<float>& field, double x, double z) {
    const PlanetBilinear b = planetBilinear(grid, x, z);
    return static_cast<float>(b.blend(field[b.cells[0]], field[b.cells[1]], field[b.cells[2]],
                                      field[b.cells[3]]));
}

} // namespace

float PreClimate::humidityAt(double x, double z) const {
    return bilinear(grid_, humidity_, x, z);
}

float PreClimate::temperatureAt(double x, double z) const {
    return bilinear(grid_, temperature_, x, z);
}

float PreClimate::precipitationAt(double x, double z) const {
    return bilinear(grid_, precipitation_, x, z);
}

std::uint64_t PreClimate::checksum() const {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t c = 0; c < grid_.cells(); ++c) {
        addBits(hash, std::bit_cast<std::uint32_t>(height_[c]));
        addBits(hash, ocean_[c]);
        addBits(hash, std::bit_cast<std::uint32_t>(precipitation_[c]));
        addBits(hash, std::bit_cast<std::uint32_t>(temperature_[c]));
        addBits(hash, std::bit_cast<std::uint32_t>(humidity_[c]));
    }
    return hash;
}

std::size_t PreClimate::memoryBytes() const {
    return sizeof(*this) + ocean_.size() +
           (height_.size() + precipitation_.size() + temperature_.size() + humidity_.size()) *
               sizeof(float);
}

std::shared_ptr<const PreClimate> PreClimate::shared(std::uint64_t geographySeed,
                                                     const GeographyParams& params,
                                                     const WorldSize& world) {
    struct Entry {
        std::uint64_t seed;
        GeographyParams params;
        WorldSize world;
        std::weak_ptr<const PreClimate> climate;
    };
    static std::mutex mutex;
    static std::vector<Entry> entries;
    static std::deque<std::shared_ptr<const PreClimate>> recent;
    const auto find = [&]() -> std::shared_ptr<const PreClimate> {
        for (const Entry& e : entries) {
            if (e.seed == geographySeed && e.params == params && e.world == world) {
                return e.climate.lock();
            }
        }
        return nullptr;
    };
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (auto found = find()) {
            return found;
        }
    }
    const std::shared_ptr<const Geography> geography =
        Geography::shared(geographySeed, params, world);
    auto built = std::make_shared<const PreClimate>(*geography);
    const std::lock_guard<std::mutex> lock(mutex);
    if (auto found = find()) {
        return found; // built meanwhile by another thread: the same bits
    }
    std::erase_if(entries, [](const Entry& e) { return e.climate.expired(); });
    entries.push_back({geographySeed, params, world, built});
    recent.push_back(built);
    if (recent.size() > 2) {
        recent.pop_front();
    }
    return built;
}

} // namespace terrain
