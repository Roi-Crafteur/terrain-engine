#include "terrain/generation/tablelands.hpp"

#include "mountain_walls.hpp"
#include "planet/planet_parallel.hpp"
#include "tableland_forms.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

// The streams of the noises of the provinces.
enum Stream : std::uint64_t {
    kSupplyTableland,
    kSupplyKarst,
    kCarbonate,
    kWarpX,
    kWarpZ,
    kEdgeTableland,
    kEdgeKarst,
};

// The rock: plateaus in layered rock (its share kStrataLow to kStrataHigh), the karst in the
// limestone of the layered rock (its share kKarstStrataLow to kKarstStrataHigh, the limestone a
// noise of kCarbonateWavelength from kCarbonateLow to kCarbonateHigh).
constexpr float kStrataLow = 0.3f;
constexpr float kStrataHigh = 0.5f;
constexpr float kKarstStrataLow = 0.25f;
constexpr float kKarstStrataHigh = 0.45f;
constexpr double kCarbonateWavelength = 500'000.0;
constexpr float kCarbonateLow = -0.4f;
constexpr float kCarbonateHigh = 0.05f;
// The share of layered rock of a cell: the mean of strataShare over kStrataSamples^2 points of it,
// then over the 3 x 3 cells around it (a region: the patches of the strata are about a hundred
// kilometers wide, finer than the cells).
constexpr int kStrataSamples = 3;
// The cells farther than kStrataSea meters out at sea hold no layered rock of the land.
constexpr double kStrataSea = -50'000.0;
// The plateaus: on the land from kLandLow to kLandHigh meters, up to kHighLow to kHighHigh; of an
// intensity of the relief from kReliefLow to kReliefHigh, up to kRoughLow to kRoughHigh (the
// mountains beyond); out of the belts (kBeltLow to kBeltHigh) and of the rifts (to kRiftHigh).
constexpr float kLandLow = 250.0f;
constexpr float kLandHigh = 700.0f;
constexpr float kHighLow = 2200.0f;
constexpr float kHighHigh = 3000.0f;
constexpr float kReliefLow = 0.06f;
constexpr float kReliefHigh = 0.11f;
constexpr float kRoughLow = 0.30f;
constexpr float kRoughHigh = 0.40f;
constexpr float kBeltLow = 0.05f;
constexpr float kBeltHigh = 0.25f;
constexpr float kRiftHigh = 0.1f;
// The karst: humid (the humidity of the pre-climate from kHumidLow to kHumidHigh) and warm enough
// (kColdLow to kColdHigh degrees), on a relief of at most kKarstRoughLow to kKarstRoughHigh.
constexpr float kHumidLow = 0.38f;
constexpr float kHumidHigh = 0.5f;
constexpr float kColdLow = 3.0f;
constexpr float kColdHigh = 8.0f;
constexpr float kKarstRoughLow = 0.32f;
constexpr float kKarstRoughHigh = 0.42f;
// The styles: dry below kAridLow to kAridHigh of humidity; tropical from kHotLow to kHotHigh
// degrees and kWetLow to kWetHigh of humidity.
constexpr float kAridLow = 0.28f;
constexpr float kAridHigh = 0.45f;
constexpr float kHotLow = 17.0f;
constexpr float kHotHigh = 21.0f;
constexpr float kWetLow = 0.55f;
constexpr float kWetHigh = 0.65f;
// The supply of each province: a slow noise of kSupplyWavelength, none at kSupplyLow, all at
// kSupplyHigh.
constexpr double kSupplyWavelength = 900'000.0;
constexpr float kSupplyLow = -0.3f;
constexpr float kSupplyHigh = 0.15f;
// The limestone lies in patches already: the supply of the karst cuts less of it.
constexpr float kKarstSupplyLow = -0.6f;
// The points: warped by up to kWarp meters (a noise of kWarpWavelength), their potential moved
// by up to kEdge (a noise of kEdgeWavelength), a province from kShareLow to kShareHigh of it.
constexpr double kWarp = 40'000.0;
constexpr double kWarpWavelength = 300'000.0;
constexpr double kEdgeWavelength = 70'000.0;
constexpr float kEdge = 0.12f;
constexpr float kShareLow = 0.35f;
constexpr float kShareHigh = 0.6f;
// A high surface may reach the points of a cell from a center whose land is kSurfaceReach meters
// high (the surface starts higher, tableland_forms.cpp), out of the belts and the rifts.
constexpr float kSurfaceReach = 1800.0f;
constexpr float kSurfaceBelt = 0.5f;
constexpr float kSurfaceRift = 0.1f;
// None within kCoastNear meters of the coast of the geography, full from kCoastFar (the coasts are
// the band of agent 33, 30 km inland at most).
constexpr double kCoastNear = 60'000.0;
constexpr double kCoastFar = 150'000.0;

noise::FbmOctaves slow(std::uint64_t seed, Stream stream, int octaves, double wavelength,
                       const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = octaves,
                              .frequency = 1.0 / wavelength,
                              .lattices = noise::OctaveLattices::Unrotated},
                             world);
}

void addBits(std::uint64_t& hash, std::uint32_t bits) {
    for (int k = 0; k < 4; ++k) {
        hash ^= (bits >> (8 * k)) & 0xFFU;
        hash *= 1099511628211ULL;
    }
}

} // namespace

struct Tablelands::Impl {
    TablelandSeeds seeds;
    std::shared_ptr<const Geography> geography;
    std::shared_ptr<const PreClimate> climate;
    bool tablelands = true;
    bool karst = true;
    mountain_walls::Noises walls;
    noise::FbmOctaves supplyTableland;
    noise::FbmOctaves supplyKarst;
    noise::FbmOctaves carbonate;
    noise::FbmOctaves warpX;
    noise::FbmOctaves warpZ;
    noise::FbmOctaves edgeTableland;
    noise::FbmOctaves edgeKarst;
    std::vector<TablelandCell> cells;
    std::vector<std::uint8_t> may; // 1 where a province may reach a point of the cell

    Impl(const TablelandSeeds& s, std::shared_ptr<const Geography> geo,
         std::shared_ptr<const PreClimate> pre)
        : seeds(s), geography(std::move(geo)), climate(std::move(pre)),
          tablelands(geography->params().tablelands), karst(geography->params().karst),
          walls(mountain_walls::makeNoises(s.walls, geography->world())),
          supplyTableland(slow(s.own, kSupplyTableland, 2, kSupplyWavelength, world())),
          supplyKarst(slow(s.own, kSupplyKarst, 2, kSupplyWavelength, world())),
          carbonate(slow(s.own, kCarbonate, 2, kCarbonateWavelength, world())),
          warpX(slow(s.own, kWarpX, 2, kWarpWavelength, world())),
          warpZ(slow(s.own, kWarpZ, 2, kWarpWavelength, world())),
          edgeTableland(slow(s.own, kEdgeTableland, 2, kEdgeWavelength, world())),
          edgeKarst(slow(s.own, kEdgeKarst, 2, kEdgeWavelength, world())) {}

    [[nodiscard]] const WorldSize& world() const { return geography->world(); }
    [[nodiscard]] float strataOf(double x, double z) const;
    [[nodiscard]] std::vector<float> regionalStrata(int threads) const;
    [[nodiscard]] TablelandCell potentials(std::size_t c, double x, double z, float strata) const;
    void build(int threads);
    void markReach();
};

// The mean share of layered rock over the cell centered on (x, z).
float Tablelands::Impl::strataOf(double x, double z) const {
    if (!(geography->sampleAt(x, z).coast > kStrataSea)) {
        return 0.0f;
    }
    const double size = climate->grid().cellSize;
    double sum = 0.0;
    for (int b = 0; b < kStrataSamples; ++b) {
        for (int a = 0; a < kStrataSamples; ++a) {
            const double px = noise::wrapCanonical(
                x + size * ((a + 0.5) / kStrataSamples - 0.5), world().x);
            const double pz = noise::wrapCanonical(
                z + size * ((b + 0.5) / kStrataSamples - 0.5), world().z);
            const float character = geography->sampleAt(px, pz).character;
            sum += mountain_walls::strataShare(walls, px, pz, character, nullptr);
        }
    }
    return static_cast<float>(sum / (kStrataSamples * kStrataSamples));
}

// The regional share of layered rock of every cell: its mean, then that of the 3 x 3 cells around.
std::vector<float> Tablelands::Impl::regionalStrata(int threads) const {
    const PlanetGrid& grid = climate->grid();
    std::vector<float> mean(grid.cells(), 0.0f);
    planet::parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                mean[grid.index(i, j)] = strataOf(grid.centerX(i), grid.centerZ(j));
            }
        }
    });
    std::vector<float> out(grid.cells(), 0.0f);
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            double sum = 0.0;
            for (int dj = -1; dj <= 1; ++dj) {
                const int row = ((j + dj) % grid.nz + grid.nz) % grid.nz;
                for (int di = -1; di <= 1; ++di) {
                    sum += mean[grid.index(grid.wrapColumn(i + di), row)];
                }
            }
            out[grid.index(i, j)] = static_cast<float>(sum / 9.0);
        }
    }
    return out;
}

TablelandCell Tablelands::Impl::potentials(std::size_t c, double x, double z,
                                           float strata) const {
    const GeographySample g = geography->sampleAt(x, z);
    if (!(g.coast > 0.0)) {
        return {};
    }
    const float rock = noise::smoothstep(kStrataLow, kStrataHigh, strata);
    const float layered = noise::smoothstep(kKarstStrataLow, kKarstStrataHigh, strata);
    const float calm = (1.0f - noise::smoothstep(kBeltLow, kBeltHigh, g.belt)) *
                       (1.0f - noise::smoothstep(0.0f, kRiftHigh, g.rift));
    const float humidity = climate->humidity(c);
    const float temperature = climate->temperature(c);
    TablelandCell out;
    out.surface = tablelands && g.land > kSurfaceReach && g.belt < kSurfaceBelt &&
                          g.rift < kSurfaceRift
                      ? 1.0f
                      : 0.0f;
    out.arid = 1.0f - noise::smoothstep(kAridLow, kAridHigh, humidity);
    out.tropical = noise::smoothstep(kHotLow, kHotHigh, temperature) *
                   noise::smoothstep(kWetLow, kWetHigh, humidity);
    if (tablelands && rock > 0.0f && calm > 0.0f) {
        const float land = noise::smoothstep(kLandLow, kLandHigh, g.land) *
                           (1.0f - noise::smoothstep(kHighLow, kHighHigh, g.land));
        const float relief = noise::smoothstep(kReliefLow, kReliefHigh, g.relief) *
                             (1.0f - noise::smoothstep(kRoughLow, kRoughHigh, g.relief));
        out.tableland = rock * calm * land * relief *
                        noise::smoothstep(kSupplyLow, kSupplyHigh, supplyTableland.fbm2D(x, z));
    }
    if (karst && layered > 0.0f && calm > 0.0f) {
        const float limestone =
            noise::smoothstep(kCarbonateLow, kCarbonateHigh, carbonate.fbm2D(x, z));
        const float wet = noise::smoothstep(kHumidLow, kHumidHigh, humidity) *
                          noise::smoothstep(kColdLow, kColdHigh, temperature);
        const float soft = 1.0f - noise::smoothstep(kKarstRoughLow, kKarstRoughHigh, g.relief);
        out.karst = layered * calm * limestone * wet * soft *
                    noise::smoothstep(kKarstSupplyLow, kSupplyHigh, supplyKarst.fbm2D(x, z));
    }
    return out;
}

void Tablelands::Impl::build(int threads) {
    const PlanetGrid& grid = climate->grid();
    const int workers = planet::resolveThreads(threads);
    const std::vector<float> strata = regionalStrata(workers);
    cells.assign(grid.cells(), TablelandCell{});
    planet::parallelRows(grid.nz, workers, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t c = grid.index(i, j);
                cells[c] = potentials(c, grid.centerX(i), grid.centerZ(j), strata[c]);
            }
        }
    });
    markReach();
}

// A point reads the cells around its warped point bilinearly: a province reaches the points of a
// cell from the cells within the warp and one more around it, where its potential and the most
// its edge adds pass the threshold.
void Tablelands::Impl::markReach() {
    const PlanetGrid& grid = climate->grid();
    std::vector<std::uint8_t> over(grid.cells(), 0);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        const float most = std::max(cells[c].tableland, cells[c].karst);
        over[c] = most + kEdge > kShareLow || cells[c].surface > 0.0f ? std::uint8_t{1}
                                                                       : std::uint8_t{0};
    }
    const int reach = static_cast<int>(std::ceil(kWarp / grid.cellSize)) + 1;
    may.assign(grid.cells(), 0);
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            if (over[grid.index(i, j)] == 0) {
                continue;
            }
            for (int dj = -reach; dj <= reach; ++dj) {
                const int row = ((j + dj) % grid.nz + grid.nz) % grid.nz;
                for (int di = -reach; di <= reach; ++di) {
                    may[grid.index(grid.wrapColumn(i + di), row)] = 1;
                }
            }
        }
    }
}

Tablelands::Tablelands(const TablelandSeeds& seeds, std::shared_ptr<const Geography> geography,
                       std::shared_ptr<const PreClimate> climate, int threads) {
    if (!geography || !climate) {
        throw std::invalid_argument("Tablelands: no geography or no pre-climate");
    }
    impl_ = std::make_unique<Impl>(seeds, std::move(geography), std::move(climate));
    impl_->build(threads);
}

Tablelands::~Tablelands() = default;

bool Tablelands::mayHold(double x, double z) const {
    return impl_->may[planetCellAt(impl_->climate->grid(), x, z)] != 0;
}

TablelandPoint Tablelands::at(double x, double z, const GeographySample& g) const {
    const Impl& m = *impl_;
    if (!mayHold(x, z)) {
        return {};
    }
    TablelandPoint p;
    // The high surfaces at the point itself (the geography holds them); the provinces from the
    // cells around the warped point, away from the coast.
    p.surface = m.tablelands ? tableland_forms::surfaceShare(g) : 0.0f;
    const std::array<float, 2> warp = m.warpX.fbm2DPair(m.warpZ, x, z);
    const double qx = noise::wrapCanonical(x + kWarp * warp[0], m.world().x);
    const double qz = noise::wrapCanonical(z + kWarp * warp[1], m.world().z);
    const PlanetBilinear b = planetBilinear(m.climate->grid(), qx, qz);
    const auto blend = [&](float TablelandCell::*field) {
        return static_cast<float>(b.blend(m.cells[b.cells[0]].*field, m.cells[b.cells[1]].*field,
                                          m.cells[b.cells[2]].*field, m.cells[b.cells[3]].*field));
    };
    if (g.coast > kCoastNear) {
        const float coast = static_cast<float>(
            noise::smoothstep(static_cast<float>(kCoastNear), static_cast<float>(kCoastFar),
                              static_cast<float>(g.coast)));
        const float tableland = blend(&TablelandCell::tableland);
        if (tableland + kEdge > kShareLow) {
            p.tableland = coast * noise::smoothstep(
                                      kShareLow, kShareHigh,
                                      tableland + kEdge * m.edgeTableland.fbm2D(x, z));
        }
        const float karst = blend(&TablelandCell::karst);
        if (karst + kEdge > kShareLow) {
            p.karst = coast * noise::smoothstep(kShareLow, kShareHigh,
                                                karst + kEdge * m.edgeKarst.fbm2D(x, z));
        }
    }
    if (!p.any()) {
        return {};
    }
    p.arid = blend(&TablelandCell::arid);
    p.tropical = blend(&TablelandCell::tropical);
    p.carbonate = noise::smoothstep(kCarbonateLow, kCarbonateHigh, m.carbonate.fbm2D(x, z));
    return p;
}

TablelandPoint Tablelands::at(double x, double z) const {
    if (!mayHold(x, z)) {
        return {};
    }
    return at(x, z, impl_->geography->sampleAt(x, z));
}

const TablelandSeeds& Tablelands::seeds() const {
    return impl_->seeds;
}

std::shared_ptr<const Geography> Tablelands::sharedGeography() const {
    return impl_->geography;
}

std::shared_ptr<const PreClimate> Tablelands::sharedClimate() const {
    return impl_->climate;
}

const PlanetGrid& Tablelands::grid() const {
    return impl_->climate->grid();
}

const TablelandCell& Tablelands::cell(std::size_t c) const {
    return impl_->cells[c];
}

const Geography& Tablelands::geography() const {
    return *impl_->geography;
}

const PreClimate& Tablelands::climate() const {
    return *impl_->climate;
}

std::uint64_t Tablelands::checksum() const {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t c = 0; c < impl_->cells.size(); ++c) {
        const TablelandCell& t = impl_->cells[c];
        for (const float v : {t.tableland, t.karst, t.arid, t.tropical, t.surface}) {
            addBits(hash, std::bit_cast<std::uint32_t>(v));
        }
        addBits(hash, impl_->may[c]);
    }
    return hash;
}

std::size_t Tablelands::memoryBytes() const {
    return sizeof(*this) + sizeof(Impl) + impl_->cells.size() * sizeof(TablelandCell) +
           impl_->may.size();
}

std::shared_ptr<const Tablelands> Tablelands::shared(const TablelandSeeds& seeds,
                                                     const GeographyParams& params,
                                                     const WorldSize& world) {
    struct Entry {
        TablelandSeeds seeds;
        GeographyParams params;
        WorldSize world;
        std::weak_ptr<const Tablelands> tablelands;
    };
    static std::mutex mutex;
    static std::vector<Entry> entries;
    static std::deque<std::shared_ptr<const Tablelands>> recent;
    const auto find = [&]() -> std::shared_ptr<const Tablelands> {
        for (const Entry& e : entries) {
            if (e.seeds == seeds && e.params == params && e.world == world) {
                return e.tablelands.lock();
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
    auto built = std::make_shared<const Tablelands>(
        seeds, Geography::shared(seeds.geography, params, world),
        PreClimate::shared(seeds.geography, params, world));
    const std::lock_guard<std::mutex> lock(mutex);
    if (auto found = find()) {
        return found; // built meanwhile by another thread: the same bits
    }
    std::erase_if(entries, [](const Entry& e) { return e.tablelands.expired(); });
    entries.push_back({seeds, params, world, built});
    recent.push_back(built);
    if (recent.size() > 2) {
        recent.pop_front();
    }
    return built;
}

} // namespace terrain
