// The coasts of the Heightfield (agent 33, coast.hpp): what they make of a point, and the relief
// without them, which the planet level and the rivers are built over.
#include "terrain/generation/heightfield.hpp"

#include "coast_faces.hpp"
#include "coast_inlets.hpp"
#include "coast_kinds.hpp"
#include "coast_lattice.hpp"
#include "coast_shape.hpp"
#include "heightfield_layers.hpp"
#include "rock_spires.hpp"
#include "mountain_walls.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terrain {

struct Heightfield::Coasts {
    std::uint64_t seed = 0;
    coast_kinds::Noises kinds;
    coast_shape::Noises shapes;
};

namespace {

// The top of a spire seen by a level of the horizon of pitch meters: the highest it stands within
// the cell of the sample (a needle narrower than the cell still shows), its details as cells of
// that pitch draw them; below every relief without one.
float spireSeen(const RockSpire& spire, double pitch) {
    if (!(spire.radius > 0.0f)) {
        return -std::numeric_limits<float>::infinity();
    }
    const rock_spires::Shape& shape = rock_spires::shapeOf(spire);
    return static_cast<float>(rock_spires::topWithin(shape, static_cast<double>(spire.dx),
                                                     static_cast<double>(spire.dz), 0.5 * pitch,
                                                     pitch));
}


// The distance to the shore grows by kMostPerMeter per meter at most (the coast signal over its
// gradient, or less where the inlets bend it).
constexpr double kMostPerMeter = 2.0;

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
// The distance to the shore of a face read kFaceStep meters away (Heightfield::distancePerMeter),
// never growing slower than kLeastPerMeter per meter.
constexpr double kFaceStep = 1.0;
constexpr double kLeastPerMeter = 0.02;

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
    const double length = dmath::hypot(gx, gz);
    const double gradient = std::max(kLeastGradient, length);
    const double distance = coast_inlets::distanceOf(g.coast / gradient, inlet);
    const coast_kinds::Weights kinds = coastKindsAt(x, z, g, memos);
    const coast_shape::Inlets inlets{g.coast,        inlet.floor,      inlet.rise,
                                     inlet.dome,     inlet.deep,       inlet.sink,
                                     inlet.glen,     inlet.islet,      inlet.rockX,
                                     inlet.rockZ,    inlet.rockRadius, inlet.rockSize,
                                     inlet.rockHeight, inlet.rockTop,  inlet.rockPhase,
                                     inlet.nextIslet, inlet.spireX,    inlet.spireZ,
                                     inlet.spireSize, inlet.spireHeight};
    // Toward the sea: down the coast signal (along x where it is flat).
    const coast_faces::Point point{x,
                                   z,
                                   distance,
                                   gradient,
                                   length > 0.0 ? -gx / length : 1.0,
                                   length > 0.0 ? -gz / length : 0.0};
    const coast_shape::Noises& noises = coasts_->shapes;
    const coast_profile::Shore shore =
        coast_shape::readsShore(kinds)
            ? coast_profile::shoreAt(noises, x, z,
                                     coast_profile::alongOf(noises, x, z, distance,
                                                            g.coast / gradient, point.airX,
                                                            point.airZ, kinds))
            : coast_profile::Shore{};
    const coast_shape::Ground shape =
        coast_shape::shapeAt(p, g, c.signal, distance, kinds, inlets, shore);
    if (memos != nullptr && memos->coastFace != nullptr) {
        coast_faces::Report report =
            coast_faces::faceAt(p, layers_->faceGrain, point, g, kinds, inlets, shore);
        if (report.faced && report.alongShore) {
            const double perMeter = distancePerMeter(x, z, g.coast / gradient, gx / gradient,
                                                     gz / gradient, distance);
            report.face.distance = static_cast<float>(report.face.distance / perMeter);
            report.face.run = static_cast<float>(report.face.run / perMeter);
        }
        const bool beyond = report.front > 0.0f && report.face.distance > report.front;
        report.faced = report.faced && !beyond;
        // The shore of a level of the horizon: within its ramp only (the distance to the shore
        // grows by kMostPerMeter per meter at most).
        if (!report.faced && memos->seenRamp > 0.0 && c.signal > 0.0 && distance > 0.0 &&
            distance < kMostPerMeter * memos->seenRamp) {
            const double perMeter = distancePerMeter(x, z, g.coast / gradient, gx / gradient,
                                                     gz / gradient, distance);
            report.shore = static_cast<float>(distance / perMeter);
        }
        *memos->coastFace = report;
    }
    c.landward = c.signal > 0.0;
    c.land = shape.land;
    c.mountains = shape.mountains;
    c.ground = shape.ground;
    c.fade = shape.fade;
    c.irregular = shape.irregular;
    c.rock = shape.rock > 0.0f || coast_faces::plungeRock(point, g, kinds, shore)
                 ? 1.0f
                 : 0.0f;
    return c;
}

void Heightfield::leaveValleys(double x, double z, HeightSample& s, Memos* memos) const {
    const double band = static_cast<double>(s.continentalness) * kContinentalScale;
    if (!coasts_ || !hydrology_ || !(band > -kCoastSeaBand - kBandSlack) ||
        !(band < params_.geography.coastRamp + kBandSlack)) {
        return;
    }
    // Whatever the relief without the coasts, the clearance there is kClearance or more: s stays.
    const Hydrology::Clearance near = hydrology_->clearanceAt(x, z);
    if (!(near.away < kClearance) && !(near.floor < std::numeric_limits<double>::infinity())) {
        return;
    }
    const auto without = [this, x, z](Memos& m) {
        m.bare = true;
        const HeightSample relief = reliefWith(x, z, &m);
        m.bare = false;
        return relief;
    };
    HeightSample b;
    if (memos != nullptr) {
        b = without(*memos);
    } else {
        Memos local; // a point alone (default-initialized, as the memos of a batch)
        b = without(local);
    }
    if (sameRelief(b, s)) {
        return;
    }
    const double clear = near.over(static_cast<double>(b.height));
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

double Heightfield::distancePerMeter(double x, double z, double base, double gx, double gz,
                                     double distance) const {
    const double ax = noise::wrapCanonical(x + kFaceStep, world_.x);
    const double az = noise::wrapCanonical(z + kFaceStep, world_.z);
    const auto at = [this](double px, double pz, double b) {
        return coast_inlets::distanceOf(
            b, inlets_ ? inlets_->effectAt(px, pz) : coast_inlets::Effect{});
    };
    const double dx = at(ax, z, base + gx * kFaceStep) - distance;
    const double dz = at(x, az, base + gz * kFaceStep) - distance;
    return std::max(kLeastPerMeter, dmath::hypot(dx, dz) / kFaceStep);
}

float Heightfield::coastFaceClear(double x, double z, float height) const {
    if (!hydrology_) {
        return 1.0f;
    }
    const double clear = hydrology_->clearanceAt(x, z).over(static_cast<double>(height));
    if (!(clear < kClearance)) {
        return 1.0f;
    }
    if (!(clear > 0.0)) {
        return 0.0f;
    }
    const double t = clear / kClearance;
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

void Heightfield::anchorCoastFace(double x, double z, float relief, RockFace& face) const {
    anchorFace(face, relief + face.foot, std::max(relief + face.foot, relief + face.top));
    face.weight *= coastFaceClear(x, z, relief);
}

HeightSample Heightfield::seenAt(double worldX, double worldZ, double pitch) const {
    coast_faces::Report report;
    Memos memos;
    memos.coastFace = &report;
    memos.seenRamp = coast_faces::shoreRampOf(pitch);
    HeightSample s = reliefWith(worldX, worldZ, &memos);
    const float relief = s.height;
    const double x = noise::wrapCanonical(worldX, world_.x);
    const double z = noise::wrapCanonical(worldZ, world_.z);
    if (hydrology_) {
        // As sampleAt: the valleys, the seas of dunes (agent 34: their height, their share and the
        // kind of their floor, filtered to the footprint of the field), then the water.
        leaveValleys(x, z, s, nullptr);
        addDunes(x, z, s);
        addSeas(x, z, s);
        applyHydrology(x, z, s, nullptr);
    }
    if (report.shore >= 0.0f && s.water == WaterKind::None) {
        s.height = coast_faces::seenShore(s.height, report.shore, pitch);
        s.waterLevel = s.height;
    }
    if (report.faced) {
        RockFace face = report.face;
        anchorCoastFace(x, z, relief, face);
        const coast_faces::Seen seen = coast_faces::seenOf(face, report.plunges, relief, pitch);
        s.height += seen.height - relief;
        s.rock = seen.rock ? 1.0f : s.rock;
        if (s.water == WaterKind::None) {
            s.waterLevel = s.height;
        }
    }
    const float spire = spireSeen(report.spire, pitch);
    if (spire > s.height) {
        s.height = spire;
        s.rock = 1.0f;
        s.water = WaterKind::None;
        s.waterLevel = spire;
    }
    return s;
}

void Heightfield::seenHeightsAt(std::span<const double> xs, std::span<const double> zs,
                                std::span<float> out, double pitch) const {
    if (xs.size() != zs.size() || xs.size() != out.size()) {
        throw std::invalid_argument("Heightfield: the points and the results differ in size");
    }
    // As heightsAt: the relief of every point, then the water of the batch; the faces of the
    // coasts reported on the way, anchored on the relief before the water.
    Memos memos;
    std::vector<HeightSample> samples(xs.size());
    std::vector<std::pair<std::size_t, coast_faces::Report>> faced;
    std::vector<std::pair<std::size_t, RockSpire>> spires;
    std::vector<std::pair<std::size_t, float>> shores;
    std::vector<float> relief;
    memos.seenRamp = coast_faces::shoreRampOf(pitch);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        coast_faces::Report report;
        memos.coastFace = &report;
        samples[i] = reliefWith(xs[i], zs[i], &memos);
        memos.coastFace = nullptr;
        if (report.faced) {
            faced.emplace_back(i, report);
            relief.push_back(samples[i].height);
        }
        if (report.spire.radius > 0.0f) {
            spires.emplace_back(i, report.spire);
        }
        if (report.shore >= 0.0f) {
            shores.emplace_back(i, report.shore);
        }
    }
    if (hydrology_) {
        waterOf(xs, zs, samples, memos);
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out[i] = samples[i].height;
    }
    for (const auto& [i, shore] : shores) {
        out[i] = samples[i].water == WaterKind::None
                     ? coast_faces::seenShore(out[i], shore, pitch)
                     : out[i];
    }
    for (std::size_t n = 0; n < faced.size(); ++n) {
        auto& [i, report] = faced[n];
        anchorCoastFace(noise::wrapCanonical(xs[i], world_.x),
                        noise::wrapCanonical(zs[i], world_.z), relief[n], report.face);
        out[i] += coast_faces::seenOf(report.face, report.plunges, relief[n], pitch).height -
                  relief[n];
    }
    for (const auto& [i, spire] : spires) {
        out[i] = std::max(out[i], spireSeen(spire, pitch));
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
