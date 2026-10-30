// The coasts of the Heightfield (agent 33, coast.hpp): what they make of a point, and the relief
// without them, which the planet level and the rivers are built over.
#include "terrain/generation/heightfield.hpp"

#include "coast_kinds.hpp"
#include "coast_shape.hpp"
#include "heightfield_layers.hpp"
#include "mountain_walls.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <memory>

namespace terrain {

struct Heightfield::Coasts {
    coast_kinds::Noises kinds;
    coast_shape::Noises shapes;
};

namespace {

// The noises of the coasts derive from the seed of the Heightfield by streams of their own, apart
// from its layers: no seed of a layer moves.
constexpr std::uint64_t kCoastStream = 0xC0A57;

// The distance to the shore is the coast signal over its gradient, read kGradientStep meters
// along x and z, never below kLeastGradient meters of signal per meter.
constexpr double kGradientStep = 8.0;
constexpr double kLeastGradient = 0.3;

// Near the rivers the coasts fade out over kClearance meters of clearance of their valleys
// (Hydrology::clearance).
constexpr double kClearance = 60.0;
// The samples that may hold coasts: their continentalness (the coast signal over kContinentalScale,
// as geographySample scales it) within the band, kBandSlack meters of signal wider.
constexpr double kContinentalScale = 1'500'000.0;
constexpr double kBandSlack = 1'000.0;

float lerp(float a, float b, float w) {
    return a + w * (b - a);
}

bool sameRelief(const HeightSample& a, const HeightSample& b) {
    return a.height == b.height && a.base == b.base && a.mountain == b.mountain &&
           a.detail == b.detail && a.mountainMask == b.mountainMask &&
           a.continentalness == b.continentalness && a.relief == b.relief && a.water == b.water;
}

} // namespace

std::shared_ptr<const Heightfield::Coasts> Heightfield::makeCoasts(std::uint64_t seed,
                                                                  const WorldSize& world) {
    auto coasts = std::make_shared<Coasts>();
    const std::uint64_t stream = noise::deriveSeed(seed, kCoastStream);
    coasts->kinds = coast_kinds::makeNoises(stream, world);
    coasts->shapes = coast_shape::makeNoises(stream, world);
    return coasts;
}

Heightfield Heightfield::withoutCoasts() const {
    Heightfield out = *this;
    out.coasts_ = nullptr;
    out.params_.geography.coasts = false;
    return out;
}

coast_kinds::Weights Heightfield::coastKindsAt(double x, double z, const GeographySample& g,
                                              Memos* memos) const {
    // sin(pi z / world.z) is the sine of z / (2 world.z) turns, as the climate reads the latitude.
    const double sine = dmath::sinTurns(z / (2.0 * world_.z));
    const double strata = mountain_walls::strataShare(layers_->walls, x, z, g.character,
                                                      memos != nullptr ? &memos->walls : nullptr);
    const coast_kinds::Ground ground{g.relief, g.character, g.rugged, static_cast<float>(strata),
                                     sine * sine};
    return coast_kinds::weightsAt(coasts_->kinds, x, z, ground);
}

Heightfield::CoastPoint Heightfield::coastPointAt(double x, double z, const GeographySample& g,
                                                  Memos* memos) const {
    const GeographyParams& p = params_.geography;
    CoastPoint c;
    c.signal = g.coast;
    c.active = coasts_ != nullptr && !(memos != nullptr && memos->bare) &&
               coast_shape::inBand(p, g.coast);
    if (!c.active) {
        c.land = coast_shape::landFactor(p, g.coast);
        c.ground = coast_shape::groundBase(p, g, g.coast);
        return c;
    }
    const double ax = noise::wrapCanonical(x + kGradientStep, world_.x);
    const double az = noise::wrapCanonical(z + kGradientStep, world_.z);
    const double gx = (geography_->sampleAt(ax, z).coast - g.coast) / kGradientStep;
    const double gz = (geography_->sampleAt(x, az).coast - g.coast) / kGradientStep;
    const double distance = g.coast / std::max(kLeastGradient, dmath::hypot(gx, gz));
    const coast_shape::Ground shape = coast_shape::shapeAt(
        p, coasts_->shapes, x, z, g, g.coast, distance, coastKindsAt(x, z, g, memos));
    c.land = shape.land;
    c.ground = shape.ground;
    return c;
}

void Heightfield::leaveValleys(double x, double z, HeightSample& s, Memos* memos) const {
    const double band = static_cast<double>(s.continentalness) * kContinentalScale;
    if (!coasts_ || !hydrology_ || !(band > -kCoastSeaBand - kBandSlack) ||
        !(band < params_.geography.coastRamp + kBandSlack) || !hydrology_->mayMatter(x, z)) {
        return;
    }
    Memos local;
    Memos& m = memos != nullptr ? *memos : local;
    m.bare = true;
    const HeightSample b = reliefWith(x, z, &m);
    m.bare = false;
    if (sameRelief(b, s)) {
        return;
    }
    const double clear = hydrology_->clearance(x, z, static_cast<double>(b.height));
    if (!(clear < kClearance)) {
        return;
    }
    if (!(clear > 0.0) || b.water != s.water || b.continentalness != s.continentalness) {
        s = b;
        return;
    }
    const double t = clear / kClearance;
    const auto w = static_cast<float>(t * t * (3.0 - 2.0 * t));
    s.mountainMask = lerp(b.mountainMask, s.mountainMask, w);
    s.relief = lerp(b.relief, s.relief, w);
    s.base = lerp(b.base, s.base, w);
    s.mountain = lerp(b.mountain, s.mountain, w);
    s.detail = lerp(b.detail, s.detail, w);
    s.height = lerp(b.height, s.height, w);
    if (s.water == WaterKind::None) {
        s.waterLevel = s.height;
    }
}

CoastSample Heightfield::coastAt(double worldX, double worldZ) const {
    CoastSample out;
    if (!geography_) {
        return out;
    }
    const double x = noise::wrapCanonical(worldX, world_.x);
    const double z = noise::wrapCanonical(worldZ, world_.z);
    const GeographySample g = geography_->sampleAt(x, z);
    const CoastPoint c = coastPointAt(x, z, g, nullptr);
    out.base = g.coast;
    out.signal = c.signal;
    out.inBand = coast_shape::inBand(params_.geography, g.coast);
    if (!c.active) {
        return out;
    }
    out.weights = coastKindsAt(x, z, g, nullptr);
    out.kind = coast_kinds::heaviest(out.weights);
    return out;
}

} // namespace terrain
