#include "terrain/generation/tablelands.hpp"

#include "mountain_walls.hpp"
#include "planet/planet_parallel.hpp"
#include "tableland_forms.hpp"
#include "tableland_front.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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
    kEdgeWarp, // and the next 7 streams: the warps of the edges, x and z of each scale
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
// by up to kEdge (a noise of kEdgeWavelength; tableland_front.hpp: the share of a province).
constexpr double kWarp = 40'000.0;
constexpr double kWarpWavelength = 300'000.0;
// The edges of the provinces wander besides by kEdgeWarps meters at the scales of
// kEdgeWarpWavelengths (at most kEdgeWarpTotal in all): bays and points from a few hundred meters to
// a hundred kilometers. These are the amplitudes of their noises: the warps deviate by about a
// fifth of them (the deviation of an fbm), 4 km at 120 km: the front is moved besides across its
// line (tableland_front.hpp), never a straight edge (an escarpment follows it,
// tableland_relief.hpp).
constexpr std::array<double, 4> kEdgeWarpWavelengths = {120'000.0, 40'000.0, 10'000.0, 2'500.0};
constexpr std::array<double, 4> kEdgeWarps = {20'000.0, 8'000.0, 2'500.0, 500.0};
constexpr double kEdgeWarpTotal = 31'000.0;
constexpr double kEdgeWavelength = 70'000.0;
// A high surface may reach the points of a cell from a center whose land is kSurfaceReach meters
// high (the surface starts higher, tableland_forms.cpp), out of the belts and the rifts.
constexpr float kSurfaceReach = 1800.0f;
constexpr float kSurfaceBelt = 0.5f;
constexpr float kSurfaceRift = 0.1f;
using tableland_front::kCoastNear;
using tableland_front::kEdge;
using tableland_front::kShareLow;

noise::FbmOctaves slow(std::uint64_t seed, std::uint64_t stream, int octaves,
                       double wavelength, const WorldSize& world) {
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
    std::array<noise::FbmOctaves, 4> edgeWarpX; // kEdgeWarpWavelengths
    std::array<noise::FbmOctaves, 4> edgeWarpZ;
    tableland_front::Swell swell; // of the fronts (tableland_front.hpp)
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
          edgeKarst(slow(s.own, kEdgeKarst, 2, kEdgeWavelength, world())),
          swell(tableland_front::makeSwell(
              noise::deriveSeed(s.own, kEdgeWarp + 2 * kEdgeWarps.size()), world())) {
        for (std::size_t k = 0; k < kEdgeWarps.size(); ++k) {
            edgeWarpX[k] = slow(s.own, kEdgeWarp + 2 * k, 2, kEdgeWarpWavelengths[k], world());
            edgeWarpZ[k] = slow(s.own, kEdgeWarp + 2 * k + 1, 2, kEdgeWarpWavelengths[k], world());
        }
    }

    [[nodiscard]] const WorldSize& world() const { return geography->world(); }
    // The point (x, z) warped (kWarp and the warps of the edges), canonical: where the provinces
    // are read for it; and the wobble of the edges there, in [-1, 1] (the fade from the coast).
    struct Warped {
        double x = 0.0;
        double z = 0.0;
        double wobble = 0.0;
    };
    [[nodiscard]] Warped warped(double x, double z) const;
    // The lifts of the potentials of the plateaus and of the karst at (x, z), warped to q: their
    // fronts moved across themselves (tableland_front.hpp).
    struct Lifts {
        float tableland = 0.0f;
        float karst = 0.0f;
    };
    [[nodiscard]] Lifts lifts(double x, double z, const Warped& q) const;
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
// cell from the cells within the warp (that of the edges too) and one more around it, where its
// potential and the most its edge adds pass the threshold.
void Tablelands::Impl::markReach() {
    const PlanetGrid& grid = climate->grid();
    // The most of a potential around a cell (the bilinear reads between the 3 x 3 cells around it)
    // and how much its neighbors differ (5 x 5: the differences of the lifts reach that far).
    const auto around = [&](int i, int j, int reach, auto&& add) {
        for (int dj = -reach; dj <= reach; ++dj) {
            const int row = ((j + dj) % grid.nz + grid.nz) % grid.nz;
            for (int di = -reach; di <= reach; ++di) {
                add(cells[grid.index(grid.wrapColumn(i + di), row)]);
            }
        }
    };
    std::vector<std::uint8_t> over(grid.cells(), 0);
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            float most = 0.0f;
            float low = 1.0f;
            around(i, j, 1, [&](const TablelandCell& t) { most = std::max({most, t.tableland, t.karst}); });
            around(i, j, 2, [&](const TablelandCell& t) { low = std::min({low, t.tableland, t.karst}); });
            const double lifted = tableland_front::mostLifted(most, most - low, grid.cellSize);
            const std::size_t c = grid.index(i, j);
            over[c] = lifted + kEdge > kShareLow || cells[c].surface > 0.0f ? std::uint8_t{1}
                                                                             : std::uint8_t{0};
        }
    }
    const int reach = static_cast<int>(std::ceil((kWarp + kEdgeWarpTotal) / grid.cellSize)) + 1;
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

Tablelands::Impl::Warped Tablelands::Impl::warped(double x, double z) const {
    const std::array<float, 2> warp = warpX.fbm2DPair(warpZ, x, z);
    double qx = x + kWarp * warp[0];
    double qz = z + kWarp * warp[1];
    double wobble = 0.0;
    for (std::size_t k = 0; k < kEdgeWarps.size(); ++k) {
        const std::array<float, 2> edge = edgeWarpX[k].fbm2DPair(edgeWarpZ[k], x, z);
        qx += kEdgeWarps[k] * static_cast<double>(edge[0]);
        qz += kEdgeWarps[k] * static_cast<double>(edge[1]);
        wobble += kEdgeWarps[k] / kEdgeWarpTotal * static_cast<double>(edge[0]);
    }
    return {noise::wrapCanonical(qx, world().x), noise::wrapCanonical(qz, world().z),
            std::clamp(wobble, -1.0, 1.0)};
}

Tablelands::Impl::Lifts Tablelands::Impl::lifts(double x, double z, const Warped& q) const {
    const double shifted = tableland_front::shiftAt(swell, x, z);
    const auto read = [&](double dx, double dz) {
        const PlanetBilinear b = planetBilinear(climate->grid(), q.x + dx, q.z + dz);
        const auto blend = [&](float TablelandCell::*field) {
            return b.blend(cells[b.cells[0]].*field, cells[b.cells[1]].*field,
                           cells[b.cells[2]].*field, cells[b.cells[3]].*field);
        };
        return std::array<double, 2>{blend(&TablelandCell::tableland), blend(&TablelandCell::karst)};
    };
    const double step = tableland_front::kStep;
    const std::array<double, 2> east = read(step, 0.0);
    const std::array<double, 2> west = read(-step, 0.0);
    const std::array<double, 2> north = read(0.0, step);
    const std::array<double, 2> south = read(0.0, -step);
    return {static_cast<float>(tableland_front::liftOf(shifted, east[0], west[0], north[0], south[0])),
            static_cast<float>(tableland_front::liftOf(shifted, east[1], west[1], north[1], south[1]))};
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
    const Impl::Warped q = m.warped(x, z);
    const PlanetBilinear b = planetBilinear(m.climate->grid(), q.x, q.z);
    const auto blend = [&](float TablelandCell::*field) {
        return static_cast<float>(b.blend(m.cells[b.cells[0]].*field, m.cells[b.cells[1]].*field,
                                          m.cells[b.cells[2]].*field, m.cells[b.cells[3]].*field));
    };
    if (g.coast > kCoastNear) {
        const float coast = tableland_front::coastFade(g.coast, q.wobble);
        const Impl::Lifts lift = m.lifts(x, z, q);
        p.tableland = tableland_front::share(blend(&TablelandCell::tableland), lift.tableland,
                                             m.edgeTableland, x, z, coast);
        p.karst = tableland_front::share(blend(&TablelandCell::karst), lift.karst, m.edgeKarst, x,
                                         z, coast);
    }
    if (!p.any()) {
        return {};
    }
    p.arid = blend(&TablelandCell::arid);
    p.tropical = blend(&TablelandCell::tropical);
    p.carbonate = noise::smoothstep(kCarbonateLow, kCarbonateHigh, m.carbonate.fbm2D(x, z));
    return p;
}

float Tablelands::tablelandShare(double x, double z, double coast) const {
    const Impl& m = *impl_;
    if (!m.tablelands || !mayHold(x, z) || !(coast > kCoastNear)) {
        return 0.0f;
    }
    // As at.
    const Impl::Warped q = m.warped(x, z);
    const PlanetBilinear b = planetBilinear(m.climate->grid(), q.x, q.z);
    const auto tableland = static_cast<float>(
        b.blend(m.cells[b.cells[0]].tableland, m.cells[b.cells[1]].tableland,
                m.cells[b.cells[2]].tableland, m.cells[b.cells[3]].tableland));
    return tableland_front::share(tableland, m.lifts(x, z, q).tableland, m.edgeTableland, x, z,
                                  tableland_front::coastFade(coast, q.wobble));
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

} // namespace terrain
