// The ergs (ergs.hpp): their mask from the field of the planet level, the relief and the water,
// their sand, the variability of their winds and the wind itself.
#include "terrain/generation/ergs.hpp"

#include "dune_waves.hpp"
#include "erg_characters.hpp"
#include "ergs_impl.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

// The mask: none at or below kMaskLow of the frayed core, full at kMaskHigh, and nothing where
// the core is 0 (none within kMaskGuard of it).
constexpr float kMaskLow = 0.12f;
constexpr float kMaskHigh = 0.4f;
constexpr float kMaskGuard = 0.06f;
// The sand: from the potential, frayed by kSandNoise of it.
constexpr float kSandLow = 0.35f;
constexpr float kSandHigh = 1.1f;
constexpr float kSandNoise = 0.45f;
// The variability: the horse latitudes (30 degrees) and the doldrums (the equator), then a noise.
constexpr double kHorseNear = 4.0;
constexpr double kHorseFar = 14.0;
constexpr double kDoldrumsNear = 2.0;
constexpr double kDoldrumsFar = 8.0;
constexpr float kVariabilityLatitude = 0.3f;
constexpr float kVariabilityBase = 0.1f;
constexpr float kVariabilityNoise = 0.55f;
// The depressions keep their dunes kDepressionMargin of their radius away from their edge.
constexpr double kDepressionMargin = 0.25;
// The edges of the dunes along the water wander outward by up to kShoreWander meters (a noise of
// kShoreWavelength) from the floors of the valleys, kLakeWander (kLakeWavelength) from the
// lakes: the straight sides of a long reach or of a square cell of a lake never show.
constexpr double kShoreWander = 1'200.0;
constexpr double kShoreWavelength = 2'500.0;
constexpr double kLakeWander = 3'000.0;
constexpr double kLakeWavelength = 12'000.0;
// How high the sand climbs the foot of the rock out of it wanders over kFootWavelength meters
// (ergs_rock.cpp).
constexpr double kFootWavelength = 90.0;
// The waves of the ergs of those characters rise this high at most (dune_waves.hpp), and their
// fades along the water stretch kFadePerMeter meters per meter of it: 12 degrees at most (the
// steepest of a Hermite curve, 1.5 times its mean).
constexpr double kWavesTallest = 300.0;
constexpr double kGiantsTallest = 800.0;
constexpr double kFadePerMeter = 7.06; // 1.5 / tan 12 degrees
// No erg in the troughs of the rifts (their walls kilometers high): faded from kRiftLow to
// kRiftHigh (the field is a little over 0 far from them).
constexpr float kRiftLow = 0.05f;
constexpr float kRiftHigh = 0.2f;
// The weights of the characters from the blocks and the slow noise: the realistic fade out from
// kRealisticEnd, the giant waves come from kGiantsStart, over kCharacterJoin.
constexpr float kRealisticEnd = 0.1f;
constexpr float kGiantsStart = 0.6f;
constexpr float kCharacterJoin = 0.3f;

// Noise streams.
enum Stream : std::uint64_t {
    kWarpX = 1,
    kWarpZ,
    kEdges,
    kSand,
    kVariability,
    kTurn,
    kSupply,
    kSinuosity,
    kDrift,
    kAmplitude,
    kSide,
    kEnds,
    kScatter,
    kShore,
    kLakeShore,
    kCharacter,
    kFoot,
};

noise::FbmOctaves octaves(std::uint64_t seed, Stream stream, int count, double wavelength,
                          const WorldSize& world) {
    noise::FbmParams p;
    p.octaves = count;
    p.frequency = 1.0 / wavelength;
    return {noise::deriveSeed(seed, stream), p, world};
}

double hermite(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

static void markDepressions(Ergs::Impl& m);
static void assignCharacters(Ergs::Impl& m);

Ergs::Ergs(std::uint64_t seed, const PlanetLevel& planet,
           const std::vector<Depression>& depressions, const ErgParams& params, RiverFloor floor,
           bool nearCells)
    : impl_(std::make_unique<Impl>()) {
    if (planet.grid().cells() == 0) {
        throw std::invalid_argument("Ergs: empty planet grid");
    }
    Impl& m = *impl_;
    static std::atomic<std::uint64_t> next{1};
    m.id = next.fetch_add(1);
    m.seed = noise::deriveSeed(seed, 0x45524753ULL); // "ERGS"
    m.params = params;
    m.climate = planet.params().climate;
    m.world = WorldSize{planet.grid().periodX(), planet.grid().periodZ()};
    m.degree = m.world.z / 180.0;
    m.depressions = depressions;
    m.planet = &planet;
    m.riverFloor = std::move(floor);
    m.floorNearCells = nearCells;
    const double warpReach = params.warpDegrees * m.degree;
    m.field = erg_field::build(planet, params, warpReach);
    markDepressions(m);
    m.warpX = octaves(m.seed, kWarpX, 2, params.warpWavelength, m.world);
    m.warpZ = octaves(m.seed, kWarpZ, 2, params.warpWavelength, m.world);
    m.edges = octaves(m.seed, kEdges, 3, params.edgeWavelength, m.world);
    m.supply = octaves(m.seed, kSupply, 2, params.supplyWavelength, m.world);
    m.sand = octaves(m.seed, kSand, 2, 2.0 * params.edgeWavelength, m.world);
    m.variability = octaves(m.seed, kVariability, 2, 300'000.0, m.world);
    m.turn = octaves(m.seed, kTurn, 2, 700'000.0, m.world);
    m.sinuosity = octaves(m.seed, kSinuosity, 2, 5'000.0, m.world);
    m.drift = octaves(m.seed, kDrift, 2, 12'000.0, m.world);
    m.amplitude = octaves(m.seed, kAmplitude, 2, 4'000.0, m.world);
    m.side = octaves(m.seed, kSide, 2, 3'000.0, m.world);
    m.ends = octaves(m.seed, kEnds, 2, 7'000.0, m.world);
    m.scatter = octaves(m.seed, kScatter, 2, 1'200.0, m.world);
    m.shore = octaves(m.seed, kShore, 2, kShoreWavelength, m.world);
    m.lakeShore = octaves(m.seed, kLakeShore, 2, kLakeWavelength, m.world);
    m.character = octaves(m.seed, kCharacter, 2, params.characterWavelength, m.world);
    m.foot = octaves(m.seed, kFoot, 3, kFootWavelength, m.world);
    assignCharacters(m);
}

Ergs::~Ergs() = default;

ErgMemos& memosOf(const Ergs::Impl& m) {
    thread_local ErgMemos memos;
    if (memos.owner != m.id) {
        memos = ErgMemos{};
        memos.owner = m.id;
    }
    return memos;
}

bool Ergs::mayHold(double x, double z) const {
    return impl_->params.enabled && erg_field::reachAt(impl_->field, x, z);
}

// The potential of the climate at the canonical (x, z) and the warp of the point.
static float potentialOf(const Ergs::Impl& m, double x, double z, double& warpX, double& warpZ) {
    ErgMemos& memo = memosOf(m);
    const double reach = m.params.warpDegrees * m.degree;
    warpX = reach * static_cast<double>(m.warpX.fbm2D(x, z, memo.warpX));
    warpZ = reach * static_cast<double>(m.warpZ.fbm2D(x, z, memo.warpZ));
    return erg_field::potentialAt(m.field, x + warpX, z + warpZ);
}

// How arid the climate of the planet is at (x, z), [0, 1] (ErgParams::rainFull and rainNone).
static float aridOf(const Ergs::Impl& m, double x, double z) {
    const PlanetBilinear c = planetBilinear(m.planet->grid(), x, z);
    const double rain = c.blend(m.planet->precipitation(c.cells[0]), m.planet->precipitation(c.cells[1]),
                                m.planet->precipitation(c.cells[2]), m.planet->precipitation(c.cells[3]));
    return 1.0f - noise::smoothstep(m.params.rainFull, m.params.rainNone, static_cast<float>(rain));
}

float Ergs::climateAt(double x, double z) const {
    if (!mayHold(x, z)) {
        return 0.0f;
    }
    double warpX = 0.0;
    double warpZ = 0.0;
    return potentialOf(*impl_, x, z, warpX, warpZ);
}

// The blocks of the field a depression may reach (its margin and fade, and a block more).
static void markDepressions(Ergs::Impl& m) {
    const erg_field::Field& f = m.field;
    m.nearDepression.assign(f.potential.size(), 0);
    for (const Depression& d : m.depressions) {
        const double reach =
            d.radius * (1.0 + kDepressionMargin) + m.params.lakeClear + m.params.lakeFade;
        const int r = static_cast<int>(std::ceil(reach / f.blockSize)) + 1;
        const int bi = static_cast<int>(std::floor((d.x + 0.5 * m.world.x) / f.blockSize));
        const int bj = static_cast<int>(std::floor((d.z + 0.5 * m.world.z) / f.blockSize));
        for (int dj = -r; dj <= r; ++dj) {
            for (int di = -r; di <= r; ++di) {
                m.nearDepression[f.block(((bi + di) % f.bx + f.bx) % f.bx,
                                         ((bj + dj) % f.bz + f.bz) % f.bz)] = 1;
            }
        }
    }
}

// The share of the dunes the depressions leave at (x, z): none on them, all beyond their margin.
static double depressionShare(const Ergs::Impl& m, double x, double z) {
    const erg_field::Field& f = m.field;
    const int bi = std::clamp(
        static_cast<int>(std::floor((x + 0.5 * m.world.x) / f.blockSize)), 0, f.bx - 1);
    const int bj = std::clamp(
        static_cast<int>(std::floor((z + 0.5 * m.world.z) / f.blockSize)), 0, f.bz - 1);
    if (m.nearDepression[f.block(bi, bj)] == 0) {
        return 1.0;
    }
    double share = 1.0;
    for (const Depression& d : m.depressions) {
        const double dx = nearestImage(d.x, x, m.world.x) - x;
        const double dz = nearestImage(d.z, z, m.world.z) - z;
        const double edge = d.radius * (1.0 + kDepressionMargin) + m.params.lakeClear;
        const double dist = dmath::hypot(dx, dz);
        if (dist < edge + m.params.lakeFade) {
            share = std::min(share, hermite(edge, edge + m.params.lakeFade, dist));
        }
    }
    return share;
}

// How far outward the edge of the dunes wanders from the water, [0, wander] meters.
static double wanderOf(const noise::FbmOctaves& n, noise::FbmOctaves::Memo& memo, double x,
                       double z, double wander) {
    // The noise stretched over its usual range (about -0.5 to 0.5), smoothly.
    return wander * static_cast<double>(noise::smoothstep(-0.5f, 0.5f, n.fbm2D(x, z, memo)));
}

// The weights of the characters at (x, z): those of the ergs of the blocks, moved by the slow
// noise toward the next character (realistic, big waves, giant waves), joined smoothly.
static void characterOf(const Ergs::Impl& m, double x, double z, ErgPoint& e) {
    const float blocks = 0.5f * erg_field::blockAt(m.field, m.waves, x, z) +
                         erg_field::blockAt(m.field, m.giants, x, z);
    const float u = std::clamp(blocks + m.params.characterMix *
                                            m.character.fbm2D(x, z, memosOf(m).character),
                               0.0f, 1.0f);
    const float realistic = 1.0f - noise::smoothstep(kRealisticEnd, kRealisticEnd + kCharacterJoin, u);
    e.giants = noise::smoothstep(kGiantsStart, kGiantsStart + kCharacterJoin, u);
    e.waves = std::max(0.0f, 1.0f - realistic - e.giants);
}

// The share of the dunes the water leaves at (x, z): none on a lake nor on the floor of a valley,
// all beyond their corridors, their edges wandering outward (never closer to the water). tallest:
// the height the dunes may reach there; their fades stretch with it.
static double waterShare(const Ergs::Impl& m, double x, double z, double tallest) {
    ErgParams p = m.params;
    p.riverFade = std::max(p.riverFade, kFadePerMeter * tallest);
    p.lakeFade = std::max(p.lakeFade, kFadePerMeter * tallest);
    const double riverFar = p.corridorClear + p.riverFade;
    const double lakeFar = p.lakeClear + p.lakeFade;
    // The floors of the valleys: near the polyline of their cells (a course strays from it by
    // kStray at most, its floor is narrower than the rest of corridorClear; the outlets of the
    // lakes stray farther): within three times corridorClear, measured: on 249,318 points over
    // the ergs of the default world, the same bits as the floor sought everywhere (12 differ with
    // corridorClear alone, none with twice it).
    const double floorFar = p.riverClear + p.riverFade + kShoreWander;
    const double floorNear = floorFar + 3.0 * p.corridorClear;
    ErgMemos& memo = memosOf(m);
    const erg_field::WaterDistance d = erg_field::waterDistance(
        m.field, x, z, std::max({riverFar, lakeFar + kLakeWander, floorNear}));
    double share = 1.0;
    if (d.lake < lakeFar + kLakeWander) {
        share = hermite(p.lakeClear, lakeFar,
                        d.lake - wanderOf(m.lakeShore, memo.lakeShore, x, z, kLakeWander));
    }
    if (!(share > 0.0)) {
        return 0.0;
    }
    if (m.riverFloor) {
        // The floor of the valleys itself, wherever it lies (never a band around the polyline of
        // the cells: a wide floor reaches past it, and the band would cut the erg along a line).
        // Sought only near the polyline of the cells (floorNear): beyond, none is that near.
        const double far = floorFar;
        const double floor =
            !m.floorNearCells || d.river < floorNear ? m.riverFloor(x, z, far) : far;
        if (floor < far) {
            share *= hermite(p.riverClear, p.riverClear + p.riverFade,
                             floor - wanderOf(m.shore, memo.shore, x, z, kShoreWander));
        }
    } else if (d.river < riverFar) {
        share *= hermite(p.corridorClear, riverFar, d.river);
    }
    return share;
}

// The share of the erg the edge of the deserts leaves at (x, z) (the warped point: the edge bends
// as the climate of the ergs does, never along a line of the planet level): sand that thins out
// toward the grass, from the climate of the land around the point.
static float desertShare(const Ergs::Impl& m, double x, double z) {
    const ErgParams& p = m.params;
    const float humidity = erg_field::blockAt(m.field, m.field.humidity, x, z);
    const float temperature = erg_field::blockAt(m.field, m.field.temperature, x, z);
    const float edge = p.edgeCool + (p.edgeHot - p.edgeCool) *
                                        noise::smoothstep(p.edgeTemperature - 4.0f,
                                                          p.edgeTemperature + 4.0f, temperature);
    return 1.0f - noise::smoothstep(edge - p.edgeBelow, edge + p.edgeAbove, humidity);
}

ErgPoint Ergs::at(double x, double z, float relief, float rift) const {
    const Impl& m = *impl_;
    if (!mayHold(x, z)) {
        return {};
    }
    // The cheap shares first (the same product either way): most land within reach of the ergs
    // is too wet or too rugged for them; their noises, the water and the depressions are dear.
    const float dry = 1.0f - noise::smoothstep(m.params.humidLow, m.params.humidHigh,
                                               m.planet->humidityAt(x, z));
    const float flat = (1.0f - noise::smoothstep(m.params.reliefLow, m.params.reliefHigh, relief)) *
                       (1.0f - noise::smoothstep(kRiftLow, kRiftHigh, rift));
    if (!(dry > 0.0f) || !(flat > 0.0f)) {
        return {};
    }
    const float arid = aridOf(m, x, z);
    if (!(arid > 0.0f)) {
        return {};
    }
    ErgMemos& memo = memosOf(m);
    const float supplied = noise::smoothstep(m.params.supplyLow, m.params.supplyHigh,
                                             m.supply.fbm2D(x, z, memo.supply));
    if (!(supplied > 0.0f)) {
        return {};
    }
    double warpX = 0.0;
    double warpZ = 0.0;
    const float potential = potentialOf(m, x, z, warpX, warpZ);
    if (!(potential > 0.0f)) {
        return {};
    }
    // The core of the erg: the arid climate where the sand is supplied; its edges frayed by a noise
    // where it passes the threshold, never beyond the climate (the guard).
    const float core = potential * supplied * arid;
    const float frayed = core + m.params.edgeNoise * m.edges.fbm2D(x, z, memo.edges);
    float mask = noise::smoothstep(kMaskLow, kMaskHigh, frayed) *
                 noise::smoothstep(0.0f, kMaskGuard, core) * flat;
    if (!(mask > 0.0f)) {
        return {};
    }
    const float desert = desertShare(m, x + warpX, z + warpZ);
    if (!(desert > 0.0f)) {
        return {};
    }
    ErgPoint e;
    characterOf(m, x, z, e);
    const double tallest = kWavesTallest * static_cast<double>(e.waves) +
                           kGiantsTallest * static_cast<double>(e.giants);
    mask *= static_cast<float>(waterShare(m, x, z, tallest) * depressionShare(m, x, z)) * dry *
            desert;
    if (!(mask > 0.0f)) {
        return {};
    }
    e.mask = mask;
    e.sand = mask * noise::smoothstep(kSandLow, kSandHigh,
                                      potential * (1.0f + kSandNoise * m.sand.fbm2D(x, z, memo.sand)));
    // The latitude of the warped point: the belts of the variable winds bend as the ergs do.
    const double warpedZ = wrapCoordinate(z + warpZ, m.world.z);
    const double latitude = 180.0 * std::abs(warpedZ) / m.world.z;
    const double horse = 1.0 - hermite(kHorseNear, kHorseFar, std::abs(latitude - 30.0));
    const double doldrums = 1.0 - hermite(kDoldrumsNear, kDoldrumsFar, latitude);
    e.variability = std::clamp(kVariabilityLatitude * static_cast<float>(std::max(horse, doldrums)) +
                                   kVariabilityBase +
                                   kVariabilityNoise * m.variability.fbm2D(x, z, memo.variability),
                               0.0f, 1.0f);
    return e;
}

std::array<double, 2> Ergs::windAt(double x, double z) const {
    return windOf(*impl_, x, z);
}

float Ergs::dunesOf(double x, double z, const ErgPoint& e) const {
    return dune_field::dunes(*impl_, x, z, e);
}

DuneKind Ergs::kindOf(const ErgPoint& e) const {
    if (!(e.mask > 0.0f)) {
        return DuneKind::None;
    }
    const float realistic = 1.0f - e.waves - e.giants;
    if (e.waves > realistic || e.giants > realistic) {
        return e.giants > e.waves ? DuneKind::Giants : DuneKind::Waves;
    }
    const dune_field::FormWeights w = dune_field::weightsOf(e);
    const std::array<std::pair<float, DuneKind>, 5> all = {{{w.barchan, DuneKind::Barchan},
                                                             {w.transverse, DuneKind::Transverse},
                                                             {w.draa, DuneKind::Draa},
                                                             {w.seif, DuneKind::Seif},
                                                             {w.star, DuneKind::Star}}};
    std::pair<float, DuneKind> best = all[0];
    for (const auto& k : all) {
        if (k.first > best.first) {
            best = k;
        }
    }
    return best.second;
}

// The characters of the ergs (erg_characters.hpp), from the cover of the blocks: their
// potential where the sand is supplied, through the threshold of the mask (before its edges).
static void assignCharacters(Ergs::Impl& m) {
    const erg_field::Field& f = m.field;
    std::vector<float> cover(f.potential.size(), 0.0f);
    for (int bj = 0; bj < f.bz; ++bj) {
        for (int bi = 0; bi < f.bx; ++bi) {
            const std::size_t b = f.block(bi, bj);
            if (!(f.potential[b] > 0.0f)) {
                continue;
            }
            const double x = -0.5 * f.grid.periodX() + (bi + 0.5) * f.blockSize;
            const double z = -0.5 * f.grid.periodZ() + (bj + 0.5) * f.blockSize;
            const float core = f.potential[b] *
                               noise::smoothstep(m.params.supplyLow, m.params.supplyHigh,
                                                 m.supply.fbm2D(x, z)) *
                               aridOf(m, x, z);
            cover[b] = noise::smoothstep(kMaskLow, kMaskHigh, core);
        }
    }
    erg_characters::Assignment a =
        erg_characters::assign(f, cover, m.seed, m.params.characterShares);
    m.waves = std::move(a.waves);
    m.giants = std::move(a.giants);
    m.ergList = std::move(a.ergs);
}

const std::vector<ErgSummary>& Ergs::ergs() const {
    return impl_->ergList;
}

const ErgParams& Ergs::params() const {
    return impl_->params;
}

const PlanetGrid& Ergs::grid() const {
    return impl_->field.grid;
}

std::size_t Ergs::memoryBytes() const {
    const erg_field::Field& f = impl_->field;
    return f.potential.size() * sizeof(float) + f.reach.size() + f.water.size() * sizeof(std::uint64_t) +
           impl_->depressions.size() * sizeof(Depression);
}

} // namespace terrain
