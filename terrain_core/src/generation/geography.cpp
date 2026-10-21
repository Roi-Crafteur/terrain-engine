#include "terrain/generation/geography.hpp"

#include "geography_stages.hpp"
#include "planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace terrain {

namespace {

using geography::Grid;

// Steps of the search of the anchor along the equator, meters.
constexpr double kAnchorStep = 1024.0;

// The coarse coast is an fbm of 5 octaves of gain kCoarseGain: the 2 broadest on the map, the 3
// others at the point, weighted by the sums of their amplitudes.
constexpr double kCoarseGain = 0.7;
constexpr double kCoarseMapWeight = 1.0 + kCoarseGain;
constexpr double kCoarsePointWeight =
    kCoarseGain * kCoarseGain * (1.0 + kCoarseGain + kCoarseGain * kCoarseGain);

// Island arcs: islands where the fbm of the arcs is above kArcThreshold - kArcBelt belt, their
// coast signal arcLift / kArcSpan times the excess (kArcFall times as steep below 0), raising the
// coast signal where it is higher, fully from a belt of kArcFull on.
constexpr double kArcThreshold = 0.75;
constexpr double kArcBelt = 0.6;
constexpr double kArcSpan = 0.4;
constexpr double kArcFall = 4.0;
constexpr float kArcFull = 0.25f;

void requirePositive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string("GeographyParams: invalid ") + name);
    }
}

// A coast stage: its amplitude must leave the band room to fade without folding the coast.
void requireStage(double wavelength, double amplitude, double band, const char* name) {
    requirePositive(wavelength, name);
    if (!std::isfinite(amplitude) || amplitude < 0.0 || !std::isfinite(band) ||
        band < 2.5 * amplitude) {
        throw std::invalid_argument(std::string("GeographyParams: invalid band of ") + name);
    }
}

void validate(const GeographyParams& p) {
    requirePositive(p.cellSize, "cellSize");
    requirePositive(p.crustWavelength, "crustWavelength");
    requirePositive(p.crustWarpWavelength, "crustWarpWavelength");
    if (p.crustOctaves < 1 || p.crustCandidates < 1 || !(p.landShare > 0.0f) ||
        !(p.landShare < 1.0f) || p.minContinents > p.maxContinents) {
        throw std::invalid_argument("GeographyParams: invalid crust");
    }
    requireStage(p.coastWavelength, p.coastAmplitude, p.coastBand, "the coast");
    requireStage(p.detailCoastWavelength, p.detailCoastAmplitude, p.detailCoastBand,
                 "the detail coast");
    requireStage(p.fineCoastWavelength, p.fineCoastAmplitude, p.fineCoastBand,
                 "the fine coast");
    requirePositive(p.ruggedWavelength, "ruggedWavelength");
    if (!(p.smoothRugged >= 0.0f) || p.smoothRugged > 1.0f) {
        throw std::invalid_argument("GeographyParams: invalid smoothRugged");
    }
    requirePositive(p.shelfWidth, "shelfWidth");
    requirePositive(p.coastRamp, "coastRamp");
    requirePositive(p.drainSlope, "drainSlope");
    if (!(p.drainJitter >= 0.0f) || !std::isfinite(p.drainJitter)) {
        throw std::invalid_argument("GeographyParams: invalid drainJitter");
    }
    requirePositive(p.provinceSize, "provinceSize");
    requirePositive(p.provinceBlend, "provinceBlend");
    requirePositive(p.valleyFloor, "valleyFloor");
    requirePositive(p.valleyWall, "valleyWall");
    requirePositive(p.mountainWall, "mountainWall");
    requirePositive(p.valleyWarpWavelength, "valleyWarpWavelength");
    if (!(p.valleyFloorLarge >= p.valleyFloor) || !std::isfinite(p.valleyFloorLarge) ||
        !(p.valleyWarp >= 0.0) || !std::isfinite(p.valleyWarp)) {
        throw std::invalid_argument("GeographyParams: invalid valleys");
    }
    requirePositive(p.beltWidth, "beltWidth");
    requirePositive(p.plateauWidth, "plateauWidth");
    requirePositive(p.arcWavelength, "arcWavelength");
    requirePositive(p.trenchWidth, "trenchWidth");
    requirePositive(p.ridgeWidth, "ridgeWidth");
    requirePositive(p.riftWidth, "riftWidth");
    if (!std::isfinite(p.arcLift) || p.arcLift < 0.0 || !std::isfinite(p.beltOffset) ||
        std::abs(p.beltOffset) > 1.0) {
        throw std::invalid_argument("GeographyParams: invalid arcs or belt offset");
    }
}

// Weight of a coast stage of amplitude a and band b at the distance d to the coast of the
// stages before it: 1 within b - 1.5 a, 0 from b on.
double bandWeight(double d, double amplitude, double band) {
    return 1.0 - noise::smoothstep(static_cast<float>(band - 1.5 * amplitude),
                                   static_cast<float>(band), static_cast<float>(std::abs(d)));
}

} // namespace

// Past the coarse stage, the island arcs of the belts at sea (their noise is only computed
// where it may raise the coast signal), then the detail and fine stages.
double Geography::coastAt(double x, double z, const GeographySample& map) const {
    const GeographyParams& p = params_;
    double c = map.coast;
    const double w1 = bandWeight(c, p.coastAmplitude, p.coastBand);
    if (w1 != 0.0) {
        // Its two broadest octaves are on the map, the three others at the point.
        c += w1 * p.coastAmplitude *
             (kCoarseMapWeight * map.coarse + kCoarsePointWeight * noises_->coarse.fbm2D(x, z)) /
             (kCoarseMapWeight + kCoarsePointWeight);
    }
    const double arcBase = kArcBelt * map.belt - kArcThreshold;
    if (map.belt > 0.0f && p.arcLift * (arcBase + 1.0) / kArcSpan > c) {
        const double excess = arcBase + noises_->arc.fbm2D(x, z);
        const double arc = p.arcLift / kArcSpan * (excess < 0.0 ? kArcFall * excess : excess);
        c += noise::smoothstep(0.0f, kArcFull, map.belt) * std::max(0.0, arc - c);
    }
    const double w2 = bandWeight(c, p.detailCoastAmplitude, p.detailCoastBand);
    if (w2 == 0.0) {
        return c;
    }
    const double rugged = map.rugged;
    c += w2 * rugged * p.detailCoastAmplitude * noises_->detail.fbm2D(x, z);
    const double w3 = bandWeight(c, p.fineCoastAmplitude, p.fineCoastBand);
    if (w3 == 0.0) {
        return c;
    }
    return c + w3 * rugged * p.fineCoastAmplitude * noises_->fine.fbm2D(x, z);
}

namespace {

std::shared_ptr<const Geography::Noises> makeNoises(std::uint64_t seed, const GeographyParams& p,
                                                    const WorldSize& world) {
    const auto fbm = [&](geography::Stream stream, int octaves, double wavelength, double gain,
                         noise::OctaveLattices lattices) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = octaves,
                                  .frequency = 1.0 / wavelength,
                                  .gain = gain,
                                  .lattices = lattices},
                                 world);
    };
    auto noises = std::make_shared<Geography::Noises>();
    noises->coarseMap = fbm(geography::kCoastCoarse, 2, p.coastWavelength, kCoarseGain,
                            noise::OctaveLattices::Unrotated);
    noises->coarse = fbm(geography::kCoastCoarsePoint, 3, 0.25 * p.coastWavelength, kCoarseGain,
                         noise::OctaveLattices::Unrotated);
    noises->detail = fbm(geography::kCoastDetail, 3, p.detailCoastWavelength, 0.55,
                         noise::OctaveLattices::RotatedFast);
    noises->fine = fbm(geography::kCoastFine, 4, p.fineCoastWavelength, 0.75,
                       noise::OctaveLattices::RotatedFast);
    noises->rugged = fbm(geography::kCoastRugged, 2, p.ruggedWavelength, 0.5,
                         noise::OctaveLattices::Unrotated);
    noises->arc = fbm(geography::kArcs, 3, p.arcWavelength, 0.5,
                      noise::OctaveLattices::RotatedFast);
    noises->valleyX = fbm(geography::kValleyWarpX, 1, p.valleyWarpWavelength, 0.5,
                          noise::OctaveLattices::RotatedFast);
    noises->valleyZ = fbm(geography::kValleyWarpZ, 1, p.valleyWarpWavelength, 0.5,
                          noise::OctaveLattices::RotatedFast);
    noises->twinValleys = noises->valleyX.twinOf(noises->valleyZ);
    return noises;
}

} // namespace

Geography::Geography(std::uint64_t seed, const GeographyParams& params, const WorldSize& world,
                     int threads)
    : params_(params), world_(world) {
    validate(params_);
    terrain::validate(world_);
    const Grid grid = geography::makeGrid(world_, params_.cellSize);
    columns_ = grid.nx;
    rows_ = grid.nz;
    sizeX_ = grid.sizeX;
    sizeZ_ = grid.sizeZ;
    noises_ = makeNoises(seed, params_, world_);
    const geography::Crust crust = geography::buildCrust(seed, params_, world_, grid,
                                                         planet::resolveThreads(threads));
    candidate_ = crust.candidate;
    continents_ = crust.continents;
    const geography::Tectonics tectonics = geography::buildTectonics(
        seed, params_, world_, grid, crust, planet::resolveThreads(threads));
    const geography::Provinces provinces = geography::buildProvinces(
        seed, params_, world_, grid, tectonics, planet::resolveThreads(threads));
    const std::vector<float> sea = geography::oceanFloor(params_, crust, tectonics);
    geography::Drainage drainage = geography::drainedLand(
        noise::deriveSeed(seed, geography::kDrainage), params_, grid, crust,
        geography::landFloor(params_, crust, tectonics, provinces));
    const std::vector<float>& land = drainage.elevation;
    valleyGeometry_ = geography::valleyGeometry(
        noise::deriveSeed(seed, geography::kValleyGeometry), grid, drainage);
    nodes_.assign(grid.nodes() * kFields, 0.0f);
    for (std::size_t k = 0; k < grid.nodes(); ++k) {
        nodes_[k * kFields + kCoast] = crust.coast[k];
        nodes_[k * kFields + kLand] = land[k];
        nodes_[k * kFields + kSea] = sea[k];
        nodes_[k * kFields + kBelt] = tectonics.belt[k];
        nodes_[k * kFields + kHills] = provinces.hills[k];
        nodes_[k * kFields + kMassif] = provinces.massif[k];
        nodes_[k * kFields + kCoarse] =
            noises_->coarseMap.fbm2D(grid.x(grid.column(k)), grid.z(grid.row(k)));
        // The ruggedness of the coasts: smoothRugged up to 1, by a slow fbm.
        nodes_[k * kFields + kRugged] =
            params_.smoothRugged +
            (1.0f - params_.smoothRugged) *
                noise::smoothstep(-0.25f, 0.35f,
                                  noises_->rugged.fbm2D(grid.x(grid.column(k)),
                                                        grid.z(grid.row(k))));
    }
    down_ = std::move(drainage.down);
    drainedArea_ = std::move(drainage.area);
    anchorX_ = chooseAnchor();
}

GeographySample Geography::mapAt(double geoX, double z) const {
    const geography::Span sx = geography::spanOf(geoX, world_.x, sizeX_);
    const geography::Span sz = geography::spanOf(z, world_.z, sizeZ_);
    const geography::Spline wx = geography::bspline(sx.t);
    const geography::Spline wz = geography::bspline(sz.t);
    std::array<double, kFields> sum{};
    double dx = 0.0;
    double dz = 0.0;
    for (int b = 0; b < 4; ++b) {
        const int row = ((sz.node - 1 + b) % rows_ + rows_) % rows_;
        for (int a = 0; a < 4; ++a) {
            const int column = ((sx.node - 1 + a) % columns_ + columns_) % columns_;
            const float* n = fieldsOf(column, row);
            const double w = wx.w[static_cast<std::size_t>(a)] * wz.w[static_cast<std::size_t>(b)];
            for (std::size_t f = 0; f < kFields; ++f) {
                sum[f] += w * n[f];
            }
            dx += wx.dw[static_cast<std::size_t>(a)] * wz.w[static_cast<std::size_t>(b)] * n[kLand];
            dz += wx.w[static_cast<std::size_t>(a)] * wz.dw[static_cast<std::size_t>(b)] * n[kLand];
        }
    }
    GeographySample s;
    s.mapCoast = static_cast<float>(sum[kCoast]);
    s.coast = sum[kCoast];
    s.land = static_cast<float>(sum[kLand]);
    s.landDX = static_cast<float>(dx / sizeX_);
    s.landDZ = static_cast<float>(dz / sizeZ_);
    s.sea = static_cast<float>(sum[kSea]);
    s.belt = static_cast<float>(sum[kBelt]);
    s.hills = static_cast<float>(sum[kHills]);
    s.massif = static_cast<float>(sum[kMassif]);
    s.rugged = static_cast<float>(sum[kRugged]);
    s.coarse = static_cast<float>(sum[kCoarse]);
    return s;
}

GeographySample Geography::sampleAt(double x, double z) const {
    GeographySample s = mapAt(wrapCoordinate(x + anchorX_, world_.x), z);
    s.coast = coastAt(x, z, s);
    return s;
}

// The shift of the map that puts a coast at (0, 0), sea on one side and land on the other along
// the equator, every kAnchorStep: among those whose land reaches 300 km inland over 100 km from
// the coast of the map (a continent, not an islet), the one with the strongest belt within
// 500 km of the coast; else any coast; the closest to no shift among equals; else none.
double Geography::chooseAnchor() const {
    const auto count = static_cast<long long>(std::floor(world_.x / kAnchorStep));
    const double half = 0.5 * world_.x;
    const auto along = [this](double shift, double distance) {
        return mapAt(wrapCoordinate(shift + distance, world_.x), 0.0);
    };
    double best = 0.0;
    float bestScore = -2.0f; // -1 for any coast, the belt for the coast of a continent
    double previous = coastAt(0.0, 0.0, mapAt(-half, 0.0));
    for (long long k = 1; k < count; ++k) {
        const double shift = -half + static_cast<double>(k) * kAnchorStep;
        const double here = coastAt(0.0, 0.0, mapAt(shift, 0.0));
        if ((previous < 0.0) != (here < 0.0)) {
            const double inland = here >= 0.0 ? 1.0 : -1.0; // the way to the land
            float score = -1.0f;
            if (along(shift, inland * 300'000.0).coast > 100'000.0) {
                score = 0.0f;
                for (int step = -10; step <= 10; ++step) {
                    score = std::max(score, along(shift, 50'000.0 * step).belt);
                }
            }
            if (score > bestScore || (score == bestScore && std::abs(shift) < std::abs(best))) {
                best = shift;
                bestScore = score;
            }
        }
        previous = here;
    }
    return best;
}

const float* Geography::fieldsOf(int column, int row) const {
    const std::size_t node = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) +
                             static_cast<std::size_t>(column);
    return &nodes_[node * kFields];
}

float Geography::node(int i, int j, Field f) const {
    return fieldsOf((i % columns_ + columns_) % columns_, (j % rows_ + rows_) % rows_)[f];
}

std::uint64_t Geography::checksum() const {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            hash ^= (value >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    add(static_cast<std::uint64_t>(columns_), 4);
    add(static_cast<std::uint64_t>(rows_), 4);
    add(std::bit_cast<std::uint64_t>(anchorX_), 8);
    for (const float v : nodes_) {
        add(std::bit_cast<std::uint32_t>(v), 4);
    }
    for (const std::int8_t v : down_) {
        add(static_cast<std::uint8_t>(v), 1);
    }
    for (const std::vector<float>* values : {&drainedArea_, &valleyGeometry_}) {
        for (const float v : *values) {
            add(std::bit_cast<std::uint32_t>(v), 4);
        }
    }
    return hash;
}

std::size_t Geography::memoryBytes() const {
    return sizeof(*this) + sizeof(Noises) +
           (nodes_.size() + drainedArea_.size() + valleyGeometry_.size()) * sizeof(float) +
           down_.size();
}

std::shared_ptr<const Geography> Geography::shared(std::uint64_t seed,
                                                   const GeographyParams& params,
                                                   const WorldSize& world) {
    struct Entry {
        std::uint64_t seed;
        GeographyParams params;
        WorldSize world;
        std::weak_ptr<const Geography> geography;
    };
    static std::mutex mutex;
    static std::vector<Entry> entries;
    static std::deque<std::shared_ptr<const Geography>> recent;
    const auto find = [&]() -> std::shared_ptr<const Geography> {
        for (const Entry& e : entries) {
            if (e.seed == seed && e.params == params && e.world == world) {
                return e.geography.lock();
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
    auto built = std::make_shared<const Geography>(seed, params, world);
    const std::lock_guard<std::mutex> lock(mutex);
    if (auto found = find()) {
        return found; // built meanwhile by another thread: the same bits
    }
    std::erase_if(entries, [](const Entry& e) { return e.geography.expired(); });
    entries.push_back({seed, params, world, built});
    recent.push_back(built);
    if (recent.size() > 2) {
        recent.pop_front();
    }
    return built;
}

} // namespace terrain
