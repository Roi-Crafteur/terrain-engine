#include "terrain/generation/seas.hpp"

#include "coast_shape.hpp"
#include "sea_floor.hpp"
#include "sea_hills.hpp"
#include "sea_plates.hpp"
#include "sea_shore.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace terrain {
namespace {

// HeightSample::continentalness times this is the coast signal (heightfield_geography.cpp).
constexpr double kContinentalScale = 1'500'000.0;
// The margin and the crust of the ocean meet rounded over this many meters.
constexpr double kCrustCorner = 150.0;
// The distance through the sea is read up to this (beyond: the oldest crust anyway).
constexpr double kFarthest = 5.0e6;
constexpr std::uint64_t kSeasStream = 0x5EA51;

// The crust of the ocean at a point, from the ridges of the corners of its cell of the map.
struct Crust {
    double crust = 0.0;
    double hills = 0.0; // rms about 1 times the amplitude of the ridge, before the sediments
    double fracture = 0.0;
    int ridge = -1;
    double ridgeDistance = -1.0;
    double age = -1.0;
    double rate = 0.0;
};

// The distances to the nearest shore and to that of the nearest large land (sea_shore.hpp).
struct Shores {
    double any = 0.0;
    double continent = 0.0;
};

// The floor moves by at most this many meters per meter of coast signal from the band.
constexpr double kFadeGradient = 0.05;

// The meters of the warped plane per meter of the world across the axis of a trench, measured
// over kAcrossStep, at least kLeastStretch.
constexpr double kAcrossStep = 100.0;
constexpr double kLeastStretch = 0.2;

struct Label {
    int ridge = -1;
    double weight = 0.0;
};

std::uint64_t fnv(std::uint64_t h, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
        h = (h ^ p[i]) * 0x100000001B3ULL;
    }
    return h;
}

template <class T> std::uint64_t fnvOf(std::uint64_t h, const std::vector<T>& v) {
    return v.empty() ? h : fnv(h, v.data(), v.size() * sizeof(T));
}

} // namespace

struct Seas::Impl {
    SeaParams params;
    WorldSize world;
    const Geography* geo = nullptr; // that of the relief (its Hydrology keeps it)
    std::uint64_t seed = 0;
    sea_plates::Plates plates;
    sea_shore::Shore shore;
    sea_hills::Noises noises;
    sea_floor::Axis axis;
    double plateauThreshold = 1.0; // of sea_hills::plateauNoise

    // The distinct ridges of the corners of the cell of (x, z) and their weights (-1: the old
    // basins no ridge reaches through the sea); the corners on land give way to the others.
    [[nodiscard]] std::array<Label, 4> labelsAt(const sea_plates::Corners& c, int& count,
                                                double& distance) const;
    [[nodiscard]] Crust crustAt(double x, double z, double footprint) const;
    // How deep the trenches dig at (x, z) and how high their outer rises lift it (sea_plates:
    // the axes of the trenches), before their strength and the fade from the shore.
    struct Trench {
        double depth = 0.0;
        double rise = 0.0;
    };
    [[nodiscard]] Trench trenchAt(double x, double z) const;
    // The distances to the shores at a point of the sea.
    [[nodiscard]] Shores shoresAt(double x, double z) const;
    // The value of the noise of the plateaus above which lies plateauShare of the nodes of the map
    // at sea that a ridge reaches (the floor of the ocean).
    [[nodiscard]] double plateauThresholdOf() const;
};

std::array<Label, 4> Seas::Impl::labelsAt(const sea_plates::Corners& c, int& count,
                                          double& distance) const {
    std::array<Label, 4> labels{};
    count = 0;
    double total = 0.0;
    distance = 0.0;
    for (std::size_t k = 0; k < 4; ++k) {
        const int ridge = plates.label[c.node[k]];
        if (ridge == sea_plates::kLand) {
            continue;
        }
        const double w = c.weight[k];
        total += w;
        distance += w * std::min(static_cast<double>(plates.distance[c.node[k]]), kFarthest);
        int at = 0;
        while (at < count && labels[static_cast<std::size_t>(at)].ridge != ridge) {
            ++at;
        }
        if (at == count) {
            labels[static_cast<std::size_t>(count++)] = {ridge, 0.0};
        }
        labels[static_cast<std::size_t>(at)].weight += w;
    }
    if (count == 0 || !(total > 0.0)) {
        count = 1;
        labels[0] = {-1, 1.0};
        distance = kFarthest;
        return labels;
    }
    distance /= total;
    for (int k = 0; k < count; ++k) {
        labels[static_cast<std::size_t>(k)].weight /= total;
    }
    return labels;
}

Crust Seas::Impl::crustAt(double x, double z, double footprint) const {
    const SeaParams& p = params;
    int count = 0;
    double grid = 0.0;
    const std::array<Label, 4> labels = labelsAt(sea_plates::cornersAt(plates, x, z), count, grid);
    const sea_plates::Point q = sea_plates::warpedAt(plates, x, z);
    Crust out;
    double squares = 0.0;
    double heaviest = -1.0;
    for (int k = 0; k < count; ++k) {
        const Label& l = labels[static_cast<std::size_t>(k)];
        squares += l.weight * l.weight;
        if (l.ridge < 0) {
            out.crust -= l.weight * p.oldBasinDepth;
            out.hills += l.weight * (1.0 - p.sedimentOld) * 0.5 * (p.hillsSlow + p.hillsFast) *
                         sea_hills::hillsAt(noises, p, q.x, q.z, footprint);
            continue;
        }
        const sea_plates::Ridge& r = plates.ridges[static_cast<std::size_t>(l.ridge)];
        const sea_plates::Frame f = sea_plates::frameOf(r, q, world);
        const sea_hills::Fracture fz = sea_hills::fractureAt(seed, l.ridge, p, f.u);
        // The axis offset by the transform faults: the age and the axial valley follow it; the
        // hills keep the frame of the ridge (no shear across the fracture zones).
        const double v = f.v - fz.offset;
        const double rate = static_cast<double>(r.rate);
        const double reach =
            std::abs(v) + (grid - std::abs(v)) * sea_floor::step(p.ageNear, p.ageFar, grid);
        const double halfRate = p.slowRate + (p.fastRate - p.slowRate) * rate;
        const double age = reach / halfRate;
        out.crust +=
            l.weight * (-sea_floor::depthOfAge(age) + sea_floor::axisRelief(axis, v, rate));
        // The sediments of the old crust bury its hills and its fracture zones (each ridge its
        // own: the share of a point blends with the weights of its ridges).
        const double buried =
            1.0 - p.sedimentOld * sea_floor::step(0.5 * p.sedimentAge, p.sedimentAge, age);
        out.hills += l.weight * buried * (p.hillsSlow + (p.hillsFast - p.hillsSlow) * rate) *
                     sea_hills::hillsAt(noises, p, f.u, f.v, footprint);
        out.fracture += l.weight * buried * fz.trough;
        if (l.weight > heaviest) {
            heaviest = l.weight;
            out.ridge = l.ridge;
            out.ridgeDistance = reach;
            out.age = age;
            out.rate = rate;
        }
    }
    // The hills of two ridges blended keep their rms.
    out.hills /= std::sqrt(squares);
    return out;
}

Shores Seas::Impl::shoresAt(double x, double z) const {
    // Exact distances (sea_shore.hpp): the coast signal over its gradient is no distance away
    // from the shore (its noises curve it too much), and the margins must keep their slopes.
    const sea_shore::Distances grid = sea_shore::distancesAt(shore, x, z);
    return {grid.any, std::max(grid.any, grid.big)};
}

Seas::Impl::Trench Seas::Impl::trenchAt(double x, double z) const {
    const sea_plates::Corners c = sea_plates::cornersAt(plates, x, z);
    std::array<int, 4> label{};
    std::array<double, 4> weight{};
    std::array<double, 4> axisAt{};
    int count = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const int l = plates.trenchLabel[c.node[k]];
        if (l < 0) {
            continue;
        }
        int at = 0;
        while (at < count && label[static_cast<std::size_t>(at)] != l) {
            ++at;
        }
        if (at == count) {
            label[static_cast<std::size_t>(count++)] = l;
        }
        weight[static_cast<std::size_t>(at)] += c.weight[k];
        axisAt[static_cast<std::size_t>(at)] += c.weight[k] * plates.trenchAxis[c.node[k]];
    }
    Trench out;
    if (count == 0) {
        return out;
    }
    // The warp of the plates stretches the meters across the axis (up to about twice): those of
    // the world, from the gradient of the distance to the axis over kAcrossStep.
    const sea_plates::Point q = sea_plates::warpedAt(plates, x, z);
    const sea_plates::Point qx =
        sea_plates::warpedAt(plates, noise::wrapCanonical(x + kAcrossStep, world.x), z);
    const sea_plates::Point qz =
        sea_plates::warpedAt(plates, x, noise::wrapCanonical(z + kAcrossStep, world.z));
    const double w = params.trenchHalfWidth;
    for (int k = 0; k < count; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const sea_plates::Ridge& r = plates.trenches[static_cast<std::size_t>(label[i])];
        const double s = r.toward * sea_plates::frameOf(r, q, world).v;
        const double gx = (r.toward * sea_plates::frameOf(r, qx, world).v - s) / kAcrossStep;
        const double gz = (r.toward * sea_plates::frameOf(r, qz, world).v - s) / kAcrossStep;
        const double stretch = std::max(kLeastStretch, dmath::hypot(gx, gz));
        // Across the axis: the V, and on the subducting side (below the axis) the outer rise.
        const double across = (s - axisAt[i] / weight[i]) / stretch;
        const double t = std::abs(across) / w;
        out.depth += t < 1.0 ? weight[i] * (1.0 - t) * (1.0 - t) : 0.0;
        const double u = (-across - params.outerRiseAt * w) / (params.outerRiseWidth * w);
        out.rise += std::abs(u) < 1.0 ? weight[i] * (1.0 - u * u) * (1.0 - u * u) : 0.0;
    }
    return out;
}

double Seas::Impl::plateauThresholdOf() const {
    std::vector<float> values;
    const geography::Grid& g = plates.grid;
    for (int j = 0; j < g.nz; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const std::size_t k = g.index(i, j);
            if (plates.label[k] < 0) {
                continue;
            }
            const double x = wrapCoordinate(g.x(i) - geo->anchorX(), world.x);
            values.push_back(static_cast<float>(sea_hills::plateauNoise(noises, x, g.z(j))));
        }
    }
    if (values.empty()) {
        return 1.0;
    }
    return static_cast<double>(geography::quantile(std::move(values), 1.0 - params.plateauShare));
}

Seas::Seas(std::uint64_t seed, const Heightfield& relief, const PlanetLevel* planet,
           const SeaParams& params, int threads) {
    const Geography* geo = relief.geography();
    if (geo == nullptr) {
        throw std::invalid_argument("Seas: the relief has no geography");
    }
    (void)planet; // the climate of the seas comes with their ice and their reefs
    auto impl = std::make_unique<Impl>();
    impl->params = params;
    impl->world = relief.world();
    impl->geo = geo;
    impl->seed = noise::deriveSeed(seed, kSeasStream);
    impl->plates = sea_plates::build(seed, *geo, threads);
    impl->shore = sea_shore::build(*geo, params.islandArea, threads);
    impl->noises = sea_hills::makeNoises(impl->seed, impl->world, params);
    impl->axis = {params.valleyDepth, params.valleyFloor, params.valleyWidth, params.highHeight,
                  params.highWidth};
    impl->plateauThreshold = impl->plateauThresholdOf();
    impl_ = std::move(impl);
}

Seas::~Seas() = default;

SeaPoint Seas::at(double x, double z, const HeightSample& s, double footprint) const {
    const Impl& m = *impl_;
    const SeaParams& p = m.params;
    SeaPoint out;
    const double coast = static_cast<double>(s.continentalness) * kContinentalScale;
    if (s.water != WaterKind::Ocean || !(coast < -p.bandStart)) {
        return out;
    }
    out.zone = true;
    out.before = static_cast<double>(s.regional);
    const Shores shores = m.shoresAt(x, z);
    const double d = shores.continent;
    out.shore = shores.any;
    out.continent = shores.continent;
    out.active =
        sea_floor::step(static_cast<double>(p.activeLow), static_cast<double>(p.activeHigh),
                        sea_plates::splineAt(m.plates, m.plates.active, x, z));
    // The margins of the large lands and of every island: the shallower.
    out.margin = sea_floor::smoothMax(
        sea_floor::marginAt(sea_hills::marginOf(m.noises, p, x, z, out.active), d),
        sea_floor::marginAt(sea_hills::islandMarginOf(m.noises, p, x, z), shores.any),
        kCrustCorner);
    const Crust c = m.crustAt(x, z, footprint);
    out.crust = c.crust;
    // An oceanic plateau raises the crust to its top (never on an active margin).
    if (p.floors) {
        out.plateau = sea_floor::step(m.plateauThreshold, m.plateauThreshold + p.plateauEdge,
                                      sea_hills::plateauNoise(m.noises, x, z)) *
                      (1.0 - out.active);
        const double top = -sea_hills::plateauTop(m.noises, p, x, z);
        if (out.plateau > 0.0 && top > out.crust) {
            out.crust += (top - out.crust) * out.plateau;
        }
    }
    out.ridge = c.ridge;
    out.ridgeDistance = c.ridgeDistance;
    out.age = c.age;
    out.rate = c.rate;
    double floor =
        p.floors ? sea_floor::smoothMax(out.margin, out.crust, kCrustCorner) : out.before;
    if (p.hills) {
        // On the crust of the ocean only (not on the margin), under the sediments of the passive
        // margins (those of the old crust: crustAt).
        const double crustShare =
            sea_floor::step(-kCrustCorner, kCrustCorner, out.crust - out.margin);
        const double sediment =
            sea_floor::step(p.sedimentFar, p.sedimentNear, d) * (1.0 - out.active);
        // Smoother on the plateaus.
        const double bare = crustShare * (1.0 - sediment) * (1.0 - 0.7 * out.plateau);
        out.hills = bare * c.hills * sea_hills::hillsAmplitude(m.noises, p, x, z);
        out.fracture = bare * c.fracture;
        floor += out.hills - out.fracture;
    }
    if (p.floors) {
        const double strength =
            std::clamp(sea_plates::splineAt(m.plates, m.plates.trenchPeak, x, z), 0.0, 1.0);
        const double near = sea_floor::step(p.trenchNear, p.trenchFull, d) * strength;
        if (near > 0.0) {
            const Impl::Trench t = m.trenchAt(x, z);
            out.trench = p.trenchDrop * near * t.depth;
            out.outerRise = p.outerRise * near * t.rise;
            floor += out.outerRise - out.trench;
        }
    }
    if (p.seamounts) {
        out.seamount = sea_hills::seamountsAt(m.noises, m.seed, m.world, p, x, z, floor);
        floor += out.seamount;
    }
    out.floor = floor;
    // Faded in from the band of the coasts over bandFull - bandStart of coast signal, or more where
    // the floor moves more (kFadeGradient: around the islets the grid of the shores misses, the
    // coast signal is that of their shore while the floor of the open sea lies far below).
    const double width =
        std::max(p.bandFull - p.bandStart, std::abs(floor - out.before) / kFadeGradient);
    out.fade = sea_floor::step(-p.bandStart, -p.bandStart - width, coast);
    return out;
}

void Seas::apply(double x, double z, HeightSample& s, double footprint) const {
    const SeaPoint p = at(x, z, s, footprint);
    if (!p.zone) {
        return;
    }
    const auto delta = static_cast<float>((p.floor - p.before) * p.fade);
    s.regional += delta;
    s.base += delta;
    s.height += delta;
}

SeaPoint Seas::mapAt(double x, double z, double footprint) const {
    const GeographySample g = impl_->geo->sampleAt(x, z);
    if (!(g.coast < 0.0)) {
        return {};
    }
    HeightSample s;
    s.continentalness = static_cast<float>(std::clamp(g.coast / kContinentalScale, -1.0, 1.0));
    s.water = WaterKind::Ocean;
    s.regional = coast_shape::groundBase(impl_->geo->params(), g, g.coast);
    SeaPoint p = at(x, z, s, footprint);
    if (p.zone) {
        p.floor = p.before + (p.floor - p.before) * p.fade;
    }
    return p;
}

const SeaParams& Seas::params() const {
    return impl_->params;
}

std::size_t Seas::memoryBytes() const {
    return sizeof(Impl) + impl_->plates.memoryBytes() + impl_->shore.memoryBytes();
}

std::uint64_t Seas::checksum() const {
    const sea_plates::Plates& p = impl_->plates;
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const std::vector<float>* f :
         {&p.offsetX, &p.offsetZ, &p.distance, &p.trench, &p.belt, &p.active}) {
        h = fnvOf(h, *f);
    }
    h = fnvOf(h, p.label);
    h = fnvOf(h, p.trenchLabel);
    h = fnvOf(h, p.trenchAxis);
    h = fnvOf(h, p.trenchPeak);
    h = fnvOf(h, impl_->shore.any);
    h = fnvOf(h, impl_->shore.big);
    for (const sea_plates::Ridge& r : p.ridges) {
        const std::array<double, 6> v{r.ax, r.az, r.mx, r.mz, r.nx, r.nz};
        h = fnv(h, v.data(), sizeof(v));
        const std::array<float, 2> w{r.strength, r.rate};
        h = fnv(h, w.data(), sizeof(w));
        h = fnv(h, &r.boundary, sizeof(r.boundary));
    }
    return h;
}

} // namespace terrain
