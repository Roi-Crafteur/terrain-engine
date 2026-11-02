// The ergs (ergs.hpp): their mask from the field of the planet level, the relief and the water,
// their sand, the variability of their winds and the wind itself.
#include "terrain/generation/ergs.hpp"

#include "ergs_impl.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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
// The bands of the winds join over kBandJoin degrees on each side of 30 and 60 degrees; the
// trades of the two hemispheres over kEquatorJoin degrees on each side of the equator.
constexpr double kBandJoin = 4.0;
constexpr double kEquatorJoin = 2.0;
// The wind turns by up to kTurnDegrees with a slow noise.
constexpr double kTurnDegrees = 18.0;
constexpr double kTurnRadians = 6.283185307179586;
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

Ergs::Ergs(std::uint64_t seed, const PlanetLevel& planet,
           const std::vector<Depression>& depressions, const ErgParams& params)
    : impl_(std::make_unique<Impl>()) {
    if (planet.grid().cells() == 0) {
        throw std::invalid_argument("Ergs: empty planet grid");
    }
    Impl& m = *impl_;
    m.seed = noise::deriveSeed(seed, 0x45524753ULL); // "ERGS"
    m.params = params;
    m.climate = planet.params().climate;
    m.world = WorldSize{planet.grid().periodX(), planet.grid().periodZ()};
    m.degree = m.world.z / 180.0;
    m.depressions = depressions;
    m.planet = &planet;
    const double warpReach = params.warpDegrees * m.degree;
    m.field = erg_field::build(planet, params, warpReach);
    m.warpX = octaves(m.seed, kWarpX, 2, params.warpWavelength, m.world);
    m.warpZ = octaves(m.seed, kWarpZ, 2, params.warpWavelength, m.world);
    m.edges = octaves(m.seed, kEdges, 3, params.edgeWavelength, m.world);
    m.supply = octaves(m.seed, kSupply, 3, params.supplyWavelength, m.world);
    m.sand = octaves(m.seed, kSand, 2, 2.0 * params.edgeWavelength, m.world);
    m.variability = octaves(m.seed, kVariability, 3, 300'000.0, m.world);
    m.turn = octaves(m.seed, kTurn, 2, 700'000.0, m.world);
    m.sinuosity = octaves(m.seed, kSinuosity, 3, 5'000.0, m.world);
    m.drift = octaves(m.seed, kDrift, 2, 12'000.0, m.world);
    m.amplitude = octaves(m.seed, kAmplitude, 2, 4'000.0, m.world);
    m.side = octaves(m.seed, kSide, 2, 3'000.0, m.world);
    m.ends = octaves(m.seed, kEnds, 2, 7'000.0, m.world);
}

Ergs::~Ergs() = default;

bool Ergs::mayHold(double x, double z) const {
    return impl_->params.enabled && erg_field::reachAt(impl_->field, x, z);
}

float Ergs::climateAt(double x, double z) const {
    const Impl& m = *impl_;
    if (!mayHold(x, z)) {
        return 0.0f;
    }
    const double reach = m.params.warpDegrees * m.degree;
    const double wx = reach * static_cast<double>(m.warpX.fbm2D(x, z));
    const double wz = reach * static_cast<double>(m.warpZ.fbm2D(x, z));
    return erg_field::potentialAt(m.field, x + wx, z + wz);
}

// The share of the dunes the depressions leave at (x, z): none on them, all beyond their margin.
static double depressionShare(const Ergs::Impl& m, double x, double z) {
    double share = 1.0;
    for (const Depression& d : m.depressions) {
        const double dx = nearestImage(d.x, x, m.world.x) - x;
        const double dz = nearestImage(d.z, z, m.world.z) - z;
        const double edge = d.radius * (1.0 + kDepressionMargin) + m.params.corridorClear;
        const double dist = dmath::hypot(dx, dz);
        if (dist < edge + m.params.corridorFade) {
            share = std::min(share, hermite(edge, edge + m.params.corridorFade, dist));
        }
    }
    return share;
}

ErgPoint Ergs::at(double x, double z, float relief) const {
    const Impl& m = *impl_;
    const float potential = climateAt(x, z);
    if (!(potential > 0.0f)) {
        return {};
    }
    // The core of the erg: the climate where the sand is supplied; its edges frayed by a noise
    // where it passes the threshold, never beyond the climate (the guard).
    const float core = potential * noise::smoothstep(m.params.supplyLow, m.params.supplyHigh,
                                                     m.supply.fbm2D(x, z));
    const float frayed = core + m.params.edgeNoise * m.edges.fbm2D(x, z);
    float mask = noise::smoothstep(kMaskLow, kMaskHigh, frayed) *
                 noise::smoothstep(0.0f, kMaskGuard, core) *
                 (1.0f - noise::smoothstep(m.params.reliefLow, m.params.reliefHigh, relief));
    if (!(mask > 0.0f)) {
        return {};
    }
    const double far = m.params.corridorClear + m.params.corridorFade;
    const double water = erg_field::waterDistance(m.field, x, z, far);
    mask *= static_cast<float>(hermite(m.params.corridorClear, far, water) *
                               depressionShare(m, x, z)) *
            (1.0f - noise::smoothstep(m.params.humidLow, m.params.humidHigh,
                                      m.planet->humidityAt(x, z)));
    if (!(mask > 0.0f)) {
        return {};
    }
    ErgPoint e;
    e.mask = mask;
    e.sand = mask * noise::smoothstep(kSandLow, kSandHigh,
                                      potential * (1.0f + kSandNoise * m.sand.fbm2D(x, z)));
    // The latitude of the warped point: the belts of the variable winds bend as the ergs do.
    const double warpedZ = wrapCoordinate(
        z + m.params.warpDegrees * m.degree * static_cast<double>(m.warpZ.fbm2D(x, z)), m.world.z);
    const double latitude = 180.0 * std::abs(warpedZ) / m.world.z;
    const double horse = 1.0 - hermite(kHorseNear, kHorseFar, std::abs(latitude - 30.0));
    const double doldrums = 1.0 - hermite(kDoldrumsNear, kDoldrumsFar, latitude);
    e.variability = std::clamp(kVariabilityLatitude * static_cast<float>(std::max(horse, doldrums)) +
                                   kVariabilityBase +
                                   kVariabilityNoise * m.variability.fbm2D(x, z),
                               0.0f, 1.0f);
    const std::array<double, 2> wind = windAt(x, z);
    e.windX = wind[0];
    e.windZ = wind[1];
    return e;
}

std::array<double, 2> Ergs::windAt(double x, double z) const {
    return windOf(*impl_, x, z);
}

// The angle (turns) toward which a band of winds blows: zonal along x, meridional poleward.
static double bandAngle(double zonal, double meridional, double poleward) {
    return dmath::atan2(poleward * meridional, zonal) / kTurnRadians;
}

static double shorter(double turns) {
    return turns - std::floor(turns + 0.5);
}

std::array<double, 2> windOf(const Ergs::Impl& m, double x, double z) {
    const double latitude = 180.0 * std::abs(z) / m.world.z;
    const double westerly = hermite(30.0 - kBandJoin, 30.0 + kBandJoin, latitude);
    const double polar = hermite(60.0 - kBandJoin, 60.0 + kBandJoin, latitude);
    // Poleward is -z in the north (z < 0), +z in the south, joined across the equator.
    const double poleward =
        2.0 * hermite(-kEquatorJoin * m.degree, kEquatorJoin * m.degree, z) - 1.0;
    // The angles of the bands, joined along the shorter arc: the wind turns at an even pace
    // across the joins, never through a calm.
    const double trades = bandAngle(-1.0, m.climate.tradeMeridional, poleward);
    const double westerlies = bandAngle(1.0, m.climate.westerlyMeridional, poleward);
    const double polars = bandAngle(-1.0, m.climate.polarMeridional, poleward);
    const double angle = trades + shorter(westerlies - trades) * westerly +
                         shorter(polars - westerlies) * polar +
                         kTurnDegrees / 360.0 * static_cast<double>(m.turn.fbm2D(x, z));
    const dmath::SineCosine d = dmath::sinCosTurns(angle);
    return {d.cosine, d.sine};
}

float Ergs::dunesAt(double x, double z, const HeightSample& s) const {
    if (s.water == WaterKind::Ocean || !mayHold(x, z)) {
        return 0.0f;
    }
    const ErgPoint e = at(x, z, s.relief);
    return e.mask > 0.0f ? dunesOf(x, z, e) : 0.0f;
}

float Ergs::dunesOf(double x, double z, const ErgPoint& e) const {
    return dune_field::dunes(*impl_, x, z, e);
}

DuneKind Ergs::kindOf(const ErgPoint& e) const {
    if (!(e.mask > 0.0f)) {
        return DuneKind::None;
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
