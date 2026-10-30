// The coasts of the Heightfield (agent 33, coast.hpp): what they make of a point, and the relief
// without them, which the planet level and the rivers are built over.
#include "terrain/generation/heightfield.hpp"

#include "coast_kinds.hpp"
#include "coast_shape.hpp"
#include "heightfield_layers.hpp"
#include "mountain_walls.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <memory>

namespace terrain {

struct Heightfield::Coasts {
    coast_kinds::Noises kinds;
};

namespace {

// The noises of the coasts derive from the seed of the Heightfield by streams of their own, apart
// from its layers: no seed of a layer moves.
constexpr std::uint64_t kCoastStream = 0xC0A57;

} // namespace

std::shared_ptr<const Heightfield::Coasts> Heightfield::makeCoasts(std::uint64_t seed,
                                                                  const WorldSize& world) {
    auto coasts = std::make_shared<Coasts>();
    coasts->kinds = coast_kinds::makeNoises(noise::deriveSeed(seed, kCoastStream), world);
    return coasts;
}

Heightfield Heightfield::withoutCoasts() const {
    Heightfield out = *this;
    out.coasts_ = nullptr;
    out.params_.geography.coasts = false;
    return out;
}

Heightfield::CoastPoint Heightfield::coastPointAt(double /*x*/, double /*z*/,
                                                  const GeographySample& g,
                                                  Memos* /*memos*/) const {
    const GeographyParams& p = params_.geography;
    CoastPoint c;
    c.signal = g.coast;
    c.land = coast_shape::landFactor(p, g.coast);
    c.ground = coast_shape::groundBase(p, g, g.coast);
    c.active = coasts_ != nullptr && coast_shape::inBand(p, g.coast);
    return c;
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
    // sin(pi z / world.z) is the sine of z / (2 world.z) turns, as the climate reads the latitude.
    const double sine = dmath::sinTurns(z / (2.0 * world_.z));
    const coast_kinds::Ground ground{
        g.relief, g.character, g.rugged,
        static_cast<float>(
            mountain_walls::strataShare(layers_->walls, x, z, g.character, nullptr)),
        sine * sine};
    out.weights = coast_kinds::weightsAt(coasts_->kinds, x, z, ground);
    out.kind = coast_kinds::heaviest(out.weights);
    return out;
}

} // namespace terrain
