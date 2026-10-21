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
}

// Weight of a coast stage of amplitude a and band b at the distance d to the coast of the
// stages before it: 1 within b - 1.5 a, 0 from b on.
double bandWeight(double d, double amplitude, double band) {
    return 1.0 - noise::smoothstep(static_cast<float>(band - 1.5 * amplitude),
                                   static_cast<float>(band), static_cast<float>(std::abs(d)));
}

} // namespace

// The fbm of the three coast stages and of the ruggedness of the coasts.
struct Geography::Noises {
    noise::FbmOctaves coarse;
    noise::FbmOctaves detail;
    noise::FbmOctaves fine;
    noise::FbmOctaves rugged;
};

double Geography::coastAt(double x, double z, double mapCoast) const {
    const GeographyParams& p = params_;
    double c = mapCoast;
    const double w1 = bandWeight(c, p.coastAmplitude, p.coastBand);
    if (w1 == 0.0) {
        return c;
    }
    c += w1 * p.coastAmplitude * noises_->coarse.fbm2D(x, z);
    const double w2 = bandWeight(c, p.detailCoastAmplitude, p.detailCoastBand);
    if (w2 == 0.0) {
        return c;
    }
    const double rugged =
        p.smoothRugged + (1.0f - p.smoothRugged) *
                             noise::smoothstep(-0.25f, 0.35f, noises_->rugged.fbm2D(x, z));
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
    noises->coarse = fbm(geography::kCoastCoarse, 4, p.coastWavelength, 0.55,
                         noise::OctaveLattices::Unrotated);
    noises->detail = fbm(geography::kCoastDetail, 3, p.detailCoastWavelength, 0.55,
                         noise::OctaveLattices::RotatedFast);
    noises->fine = fbm(geography::kCoastFine, 5, p.fineCoastWavelength, 0.6,
                       noise::OctaveLattices::RotatedFast);
    noises->rugged = fbm(geography::kCoastRugged, 2, p.ruggedWavelength, 0.5,
                         noise::OctaveLattices::Unrotated);
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
    const std::vector<float> sea = geography::oceanFloor(params_, grid, crust);
    const std::vector<float> land =
        geography::drainedLand(params_, grid, crust, geography::landFloor(params_, crust));
    nodes_.assign(grid.nodes() * kFields, 0.0f);
    for (std::size_t k = 0; k < grid.nodes(); ++k) {
        nodes_[k * kFields + kCoast] = crust.coast[k];
        nodes_[k * kFields + kLand] = land[k];
        nodes_[k * kFields + kSea] = sea[k];
    }
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
    return s;
}

GeographySample Geography::sampleAt(double x, double z) const {
    GeographySample s = mapAt(wrapCoordinate(x + anchorX_, world_.x), z);
    s.coast = coastAt(x, z, s.coast);
    return s;
}

// The shift of the map that puts a coast at (0, 0), sea to the west and land to the east: on
// the equator every kAnchorStep, the one closest to no shift whose land reaches 300 km east
// over 100 km from the coast of the map (a continent, not an islet); else any coast; else none.
double Geography::chooseAnchor() const {
    const auto count = static_cast<long long>(std::floor(world_.x / kAnchorStep));
    const double half = 0.5 * world_.x;
    double best = 0.0;
    double fallback = 0.0;
    bool found = false;
    bool any = false;
    double previous = coastAt(0.0, 0.0, mapAt(-half, 0.0).coast);
    for (long long k = 1; k < count; ++k) {
        const double shift = -half + static_cast<double>(k) * kAnchorStep;
        const double here = coastAt(0.0, 0.0, mapAt(shift, 0.0).coast);
        if (previous < 0.0 && here >= 0.0) {
            const double inland = mapAt(wrapCoordinate(shift + 300'000.0, world_.x), 0.0).coast;
            if (!any || std::abs(shift) < std::abs(fallback)) {
                fallback = shift;
                any = true;
            }
            if (inland > 100'000.0 && (!found || std::abs(shift) < std::abs(best))) {
                best = shift;
                found = true;
            }
        }
        previous = here;
    }
    return found ? best : fallback;
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
    return hash;
}

std::size_t Geography::memoryBytes() const {
    return sizeof(*this) + nodes_.size() * sizeof(float) + sizeof(Noises);
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
