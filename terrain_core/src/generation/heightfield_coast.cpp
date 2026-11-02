// The coasts of the Heightfield (agent 33, coast.hpp): what they make of a point, and the relief
// without them, which the planet level and the rivers are built over.
#include "terrain/generation/heightfield.hpp"

#include "coast_inlets.hpp"
#include "coast_kinds.hpp"
#include "coast_lattice.hpp"
#include "coast_shape.hpp"
#include "heightfield_layers.hpp"
#include "mountain_walls.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace terrain {

struct Heightfield::Coasts {
    std::uint64_t seed = 0;
    coast_kinds::Noises kinds;
    coast_shape::Noises shapes;
};

namespace {

// The noises of the coasts derive from the seed of the Heightfield by streams of their own, apart
// from its layers: no seed of a layer moves.
constexpr std::uint64_t kCoastStream = 0xC0A57;

// The distance to the shore is the coast signal over its gradient, read kGradientStep meters
// along x and z (the signal of the map is a float: its rounding over that step moves the distance
// by millimeters near the shore), never below kLeastGradient meters of signal per meter (half the
// usual gradient): where the signal flattens (the middle of a narrow peninsula) the distance never
// runs away, and the shapes there only get wider, never steeper.
constexpr double kGradientStep = 64.0;
constexpr double kLeastGradient = 1.5;

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
    coasts->seed = stream;
    coasts->kinds = coast_kinds::makeNoises(stream, world);
    coasts->shapes = coast_shape::makeNoises(stream, world);
    return coasts;
}

Heightfield Heightfield::withoutCoasts() const {
    Heightfield out = *this;
    out.coasts_ = nullptr;
    out.inlets_ = nullptr;
    out.params_.geography.coasts = false;
    return out;
}

namespace {

// The kinds of coast at a canonical point of the geography g, and the share of strata there.
coast_inlets::Site kindsOf(const coast_kinds::Noises& kinds, const mountain_walls::Noises& walls,
                           const WorldSize& world, double x, double z, const GeographySample& g,
                           mountain_walls::Memo* memo) {
    // sin(pi z / world.z) is the sine of z / (2 world.z) turns, as the climate reads the latitude.
    const double sine = dmath::sinTurns(z / (2.0 * world.z));
    const double strata = mountain_walls::strataShare(walls, x, z, g.character, memo);
    const coast_kinds::Ground ground{g.relief, g.character, g.rugged, static_cast<float>(strata),
                                     sine * sine};
    return {g.coast, coast_kinds::weightsAt(kinds, x, z, ground), static_cast<float>(strata),
            0.0f};
}

} // namespace

std::shared_ptr<const coast_lattice::Lattices> Heightfield::makeInlets() const {
    coast_inlets::World w;
    w.size = world_;
    w.seed = coasts_->seed;
    w.band = params_.geography.coastRamp;
    const auto geography = geography_;
    const WorldSize world = world_;
    w.coast = [geography, world](double x, double z) {
        return geography->sampleAt(wrapCoordinate(x, world.x), wrapCoordinate(z, world.z)).coast;
    };
    // The relief the inlets are cut in: this Heightfield, neither inlets nor rivers in it.
    auto bare = std::make_shared<Heightfield>(*this);
    bare->inlets_ = nullptr;
    bare->hydrology_ = nullptr;
    w.height = [bare](double x, double z) { return static_cast<double>(bare->heightAt(x, z)); };
    const auto layers = layers_;
    const auto coasts = coasts_;
    w.site = [geography, layers, coasts, world](double x, double z) {
        const double cx = wrapCoordinate(x, world.x);
        const double cz = wrapCoordinate(z, world.z);
        const GeographySample g = geography->sampleAt(cx, cz);
        coast_inlets::Site site = kindsOf(coasts->kinds, layers->walls, world, cx, cz, g, nullptr);
        site.top = static_cast<float>(coast_shape::shoreTop(coasts->shapes, cx, cz, g,
                                                            site.weights));
        return site;
    };
    if (hydrology_) {
        const auto hydrology = hydrology_;
        w.clearance = [hydrology, world](double x, double z) {
            const double cx = wrapCoordinate(x, world.x);
            const double cz = wrapCoordinate(z, world.z);
            return hydrology->clearance(cx, cz,
                                        static_cast<double>(hydrology->relief().heightAt(cx, cz)));
        };
    }
    return std::make_shared<const coast_lattice::Lattices>(std::move(w));
}

std::vector<CoastSite> Heightfield::inletsIn(double minX, double minZ, double maxX,
                                             double maxZ) const {
    return inlets_ ? inlets_->sitesIn(minX, minZ, maxX, maxZ) : std::vector<CoastSite>{};
}

std::vector<CoastStack> Heightfield::stacksIn(double minX, double minZ, double maxX,
                                              double maxZ) const {
    return inlets_ ? inlets_->stacksIn(minX, minZ, maxX, maxZ) : std::vector<CoastStack>{};
}

std::vector<CoastSite> Heightfield::harborsIn(double minX, double minZ, double maxX,
                                              double maxZ) const {
    std::vector<CoastSite> out = inletsIn(minX, minZ, maxX, maxZ);
    out.erase(std::remove_if(out.begin(), out.end(), [](const CoastSite& s) { return !s.harbor; }),
              out.end());
    return out;
}

std::vector<CoastSite> Heightfield::anchoragesIn(double minX, double minZ, double maxX,
                                                 double maxZ) const {
    std::vector<CoastSite> out = inletsIn(minX, minZ, maxX, maxZ);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const CoastSite& s) { return !s.anchorage; }),
              out.end());
    return out;
}

coast_kinds::Weights Heightfield::coastKindsAt(double x, double z, const GeographySample& g,
                                              Memos* memos) const {
    return kindsOf(coasts_->kinds, layers_->walls, world_, x, z, g,
                   memos != nullptr ? &memos->walls : nullptr)
        .weights;
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
        c.mountains = c.land;
        c.ground = coast_shape::groundBase(p, g, g.coast);
        return c;
    }
    // The coast signal of the world: that of the geography, the inlets and stacks cut in it.
    const coast_inlets::Effect inlet = inlets_ ? inlets_->effectAt(x, z) : coast_inlets::Effect{};
    c.signal = coast_inlets::signalOf(g.coast, inlet);
    const double ax = noise::wrapCanonical(x + kGradientStep, world_.x);
    const double az = noise::wrapCanonical(z + kGradientStep, world_.z);
    const double gx = (geography_->sampleAt(ax, z).coast - g.coast) / kGradientStep;
    const double gz = (geography_->sampleAt(x, az).coast - g.coast) / kGradientStep;
    const double distance = coast_inlets::distanceOf(
        g.coast / std::max(kLeastGradient, dmath::hypot(gx, gz)), inlet);
    const coast_shape::Ground shape = coast_shape::shapeAt(
        p, coasts_->shapes, x, z, g, c.signal, distance, coastKindsAt(x, z, g, memos),
        {g.coast, inlet.floor, inlet.rise, inlet.dome, inlet.deep, inlet.sink, inlet.glen,
         inlet.islet});
    c.landward = c.signal > 0.0;
    c.land = shape.land;
    c.mountains = shape.mountains;
    c.ground = shape.ground;
    c.fade = shape.fade;
    c.irregular = shape.irregular;
    c.rock = shape.rock;
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
