#include "terrain/generation/voronoi.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "voronoi_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace terrain {

namespace {

using voronoi_layout::wrapOffset;

// Independent streams of the layer. New streams go at the end.
enum Stream : std::uint64_t { kLayout, kWarpX, kWarpZ, kDetailWarpX, kDetailWarpZ, kCoast };

// The neighborhood of a point holds the plates whose gap (below) is under the gate width, at
// most kMaxNear of them, the nearest first. kGateMargin widens the gate beyond the strict
// bound computed in gateWidth.
constexpr double kGateMargin = 1.25;
constexpr int kMaxNear = 16;

// (1 - t^2)^2 inside [-1, 1], 0 outside: a C1 bump of half width 1 peaking at 1.
float bump(double t) {
    if (std::abs(t) >= 1.0) {
        return 0.0f;
    }
    const double u = 1.0 - t * t;
    return static_cast<float>(u * u);
}

// 1 up to start, 0 from end on, smooth in between.
float falloff(double start, double end, double x) {
    return 1.0f - noise::smoothstep(static_cast<float>(start), static_cast<float>(end),
                                    static_cast<float>(x));
}

bool isLand(PlateKind kind) {
    return kind == PlateKind::Continent;
}

void requireLength(double value, const char* name, bool allowZero = false) {
    if (!std::isfinite(value) || value < 0.0 || (!allowZero && value == 0.0)) {
        throw std::invalid_argument(std::string("VoronoiParams: invalid ") + name);
    }
}

void validate(const VoronoiParams& p) {
    if (p.minPlates < 4 || p.maxPlates < p.minPlates || p.maxPlates > TectonicPlates::kMaxPlates ||
        p.lloydIterations < 0) {
        throw std::invalid_argument("VoronoiParams: invalid plate count or Lloyd iterations");
    }
    requireLength(p.warpWavelength, "warpWavelength");
    requireLength(p.warpAmplitude, "warpAmplitude", true);
    requireLength(p.detailWarpWavelength, "detailWarpWavelength");
    requireLength(p.detailWarpAmplitude, "detailWarpAmplitude", true);
    requireLength(p.coastWidth, "coastWidth");
    requireLength(p.coastNoiseWavelength, "coastNoiseWavelength");
    requireLength(p.collisionWidth, "collisionWidth");
    requireLength(p.riftWidth, "riftWidth");
    requireLength(p.riftFlankWidth, "riftFlankWidth");
    requireLength(p.junctionFade, "junctionFade");
}

// A plate near the warped point: offset to the nearest image of its site, squared distance,
// and gap = (distance2 - nearest distance2) / (2 * cellDiameter). For a neighbor of the
// nearest plate the gap is about the distance to their boundary, and it is continuous in the
// point, whichever plate is the nearest: every blend and effect below is built on it.
// Trivial on purpose: neighborhood() fills an array of kMaxPlates of them for every sample.
struct Near {
    int plate;
    double dx;
    double dz;
    double distance2;
    double gap;
};

struct Neighborhood {
    std::array<Near, kMaxNear> near{}; // by increasing distance; near[0] is the nearest plate
    int count = 0;
    Near second{}; // second nearest plate, whether it is in near or not
};

// Plate k seen from q, a point of the canonical range.
Near nearPlate(const TectonicPlates& plates, int k, TectonicPlates::Point q) {
    const Plate& plate = plates.plates()[static_cast<std::size_t>(k)];
    const double dx = voronoi_layout::shortOffset(plate.x - q.x, plates.world().x);
    const double dz = voronoi_layout::shortOffset(plate.z - q.z, plates.world().z);
    return {k, dx, dz, dx * dx + dz * dz, 0.0};
}

bool closer(const Near& a, const Near& b) {
    return a.distance2 < b.distance2 || (a.distance2 == b.distance2 && a.plate < b.plate);
}

Neighborhood neighborhood(const TectonicPlates& plates, TectonicPlates::Point q, double gate) {
    const int count = static_cast<int>(plates.plates().size());
    const TectonicPlates::Point wrapped{noise::wrapCanonical(q.x, plates.world().x),
                                        noise::wrapCanonical(q.z, plates.world().z)};
    std::array<Near, TectonicPlates::kMaxPlates> all;
    for (int k = 0; k < count; ++k) {
        all[static_cast<std::size_t>(k)] = nearPlate(plates, k, wrapped);
    }
    const auto end = all.begin() + count;
    std::partial_sort(all.begin(), all.begin() + 2, end, closer);
    const double nearest2 = all[0].distance2;
    const double scale = 2.0 * plates.cellDiameter();
    Neighborhood hood;
    hood.second = all[1];
    for (auto it = all.begin(); it != end; ++it) {
        it->gap = (it->distance2 - nearest2) / scale;
    }
    const auto inside =
        std::partition(all.begin(), end, [gate](const Near& n) { return n.gap < gate; });
    std::sort(all.begin(), inside, closer);
    hood.count = static_cast<int>(std::min<std::ptrdiff_t>(inside - all.begin(), kMaxNear));
    std::copy_n(all.begin(), hood.count, hood.near.begin());
    return hood;
}

// Distance from the warped point to the bisector of two sites (exact for their images).
double bisectorDistance(const Near& a, const Near& b) {
    const double spacing = dmath::hypot(a.dx - b.dx, a.dz - b.dz);
    return std::abs(a.distance2 - b.distance2) / (2.0 * spacing);
}

// Gate width of the neighborhoods. A plate outside it has no blend weight (its gap is above
// coastWidth) and no boundary effect: a boundary acts within the widest effect of its
// bisector, i.e. within widest * spacing / cellDiameter of gap past the closer plate of the
// pair, and that plate is at most junctionFade past the nearest one. So the neighborhood
// changes without any jump in the samples.
double gateWidth(const VoronoiParams& p, double maxSpacing, double cellDiameter) {
    const double widest = std::max({p.coastWidth, p.collisionWidth, p.riftWidth, p.riftFlankWidth});
    return kGateMargin * (p.junctionFade + widest * std::max(1.0, maxSpacing / cellDiameter));
}

float kindLevel(const VoronoiParams& p, PlateKind kind) {
    switch (kind) {
    case PlateKind::Continent:
        return p.continentLevel;
    case PlateKind::InlandSea:
        return p.inlandSeaLevel;
    case PlateKind::ContinentalShelf:
        return p.shelfLevel;
    case PlateKind::Ocean:
        break;
    }
    return p.oceanLevel;
}

// Plate levels blended over coastWidth on each side of the boundaries. The nearest plate
// always weighs 1, so the weights never sum to 0.
float blendedLevel(const TectonicPlates& plates, const Neighborhood& hood) {
    const VoronoiParams& p = plates.params();
    float weights = 0.0f;
    float level = 0.0f;
    for (int i = 0; i < hood.count; ++i) {
        const Near& n = hood.near[static_cast<std::size_t>(i)];
        const float w = falloff(0.0, p.coastWidth, n.gap);
        weights += w;
        level += w * kindLevel(p, plates.plates()[static_cast<std::size_t>(n.plate)].kind);
    }
    return level / weights;
}

// Weight of the boundary between near[i] and near[j], i < j: 1 while no other plate is
// closer than both of them, fading to 0 over junctionFade once a third plate is closer. The
// effects of a boundary thus stop at its junctions instead of running along the bisector
// into the third plate. Continuous: it only depends on squared distances.
float boundaryWeight(const Neighborhood& hood, int i, int j, const VoronoiParams& p,
                     double cellDiameter) {
    const int other = i > 0 ? 0 : (j > 1 ? 1 : 2);
    if (other >= hood.count) {
        return 1.0f;
    }
    const double inside = hood.near[static_cast<std::size_t>(i)].distance2 -
                          hood.near[static_cast<std::size_t>(other)].distance2;
    return falloff(0.0, p.junctionFade, inside / (2.0 * cellDiameter));
}

struct Rift {
    float trough = 0.0f;
    float flank = 0.0f; // continental flanks
    float ridge = 0.0f; // oceanic flanks
};

// Collision belts, rifts and coasts of every boundary between two plates of the
// neighborhood. Taking the strongest boundary (max) keeps every field continuous where
// several boundaries meet.
void addBoundaryEffects(const TectonicPlates& plates, const Neighborhood& hood, PlateSample& s) {
    const VoronoiParams& p = plates.params();
    Rift rift;
    for (int i = 0; i < hood.count; ++i) {
        const Near& a = hood.near[static_cast<std::size_t>(i)];
        for (int j = i + 1; j < hood.count; ++j) {
            const Near& b = hood.near[static_cast<std::size_t>(j)];
            const int index = plates.boundaryIndex(a.plate, b.plate);
            const float weight =
                index < 0 ? 0.0f : boundaryWeight(hood, i, j, p, plates.cellDiameter());
            if (weight <= 0.0f) {
                continue;
            }
            const PlateBoundary& boundary = plates.boundaries()[static_cast<std::size_t>(index)];
            const double distance = bisectorDistance(a, b);
            const float strength = std::abs(boundary.convergence) * weight;
            const bool landA = isLand(plates.plates()[static_cast<std::size_t>(a.plate)].kind);
            const bool landB = isLand(plates.plates()[static_cast<std::size_t>(b.plate)].kind);
            if (boundary.kind == BoundaryKind::Collision) {
                s.collision = std::max(s.collision, strength * bump(distance / p.collisionWidth));
            } else if (boundary.kind == BoundaryKind::Divergence) {
                const float flank = strength * bump(distance / p.riftFlankWidth);
                rift.trough = std::max(rift.trough, strength * bump(distance / p.riftWidth));
                float& side = landA || landB ? rift.flank : rift.ridge;
                side = std::max(side, flank);
            }
            if (landA != landB) {
                s.coast = std::max(s.coast, weight * bump(distance / p.coastWidth));
            }
        }
    }
    s.rift = p.riftFlankHeight * rift.flank + p.oceanRidgeHeight * rift.ridge -
             p.riftDepth * rift.trough;
}

} // namespace

TectonicPlates::TectonicPlates(std::uint64_t seed, const VoronoiParams& params,
                               const WorldSize& world)
    : params_(params), world_(world) {
    validate(params);
    terrain::validate(world);
    voronoi_layout::Layout layout =
        voronoi_layout::buildLayout(noise::deriveSeed(seed, kLayout), params, world);
    cellDiameter_ = layout.cellDiameter;
    plates_ = std::move(layout.plates);
    boundaries_ = std::move(layout.boundaries);
    const std::size_t count = plates_.size();
    boundaryIndex_.assign(count * count, -1);
    double maxSpacing = 0.0; // between the sites of two neighbors
    for (std::size_t k = 0; k < boundaries_.size(); ++k) {
        const auto a = static_cast<std::size_t>(boundaries_[k].a);
        const auto b = static_cast<std::size_t>(boundaries_[k].b);
        boundaryIndex_[a * count + b] = static_cast<int>(k);
        boundaryIndex_[b * count + a] = static_cast<int>(k);
        maxSpacing =
            std::max(maxSpacing, dmath::hypot(wrapOffset(plates_[b].x - plates_[a].x, world_.x),
                                              wrapOffset(plates_[b].z - plates_[a].z, world_.z)));
    }
    gateWidth_ = gateWidth(params_, maxSpacing, cellDiameter_);
    // The noises of warp and sampleAt, with the same parameters (FbmOctaves).
    const auto noiseOf = [seed, &world](Stream stream, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = 3, .frequency = 1.0 / wavelength}, world);
    };
    if (params_.warpAmplitude > 0.0) {
        warps_[0] = noiseOf(kWarpX, params_.warpWavelength);
        warps_[1] = noiseOf(kWarpZ, params_.warpWavelength);
    }
    if (params_.detailWarpAmplitude > 0.0) {
        warps_[2] = noiseOf(kDetailWarpX, params_.detailWarpWavelength);
        warps_[3] = noiseOf(kDetailWarpZ, params_.detailWarpWavelength);
    }
    if (params_.coastRoughness > 0.0f) {
        coast_ = noiseOf(kCoast, params_.coastNoiseWavelength);
    }
}

TectonicPlates::Point TectonicPlates::warp(double x, double z) const {
    return warpWith(x, z, nullptr);
}

TectonicPlates::Point TectonicPlates::warpWith(double x, double z, Memos* memos) const {
    const auto fbm = [x, z, memos, this](std::size_t i) {
        return memos ? warps_[i].fbm2D(x, z, memos->warps[i]) : warps_[i].fbm2D(x, z);
    };
    Point q{x, z};
    if (params_.warpAmplitude > 0.0) {
        q.x += params_.warpAmplitude * fbm(0);
        q.z += params_.warpAmplitude * fbm(1);
    }
    if (params_.detailWarpAmplitude > 0.0) {
        q.x += params_.detailWarpAmplitude * fbm(2);
        q.z += params_.detailWarpAmplitude * fbm(3);
    }
    return q;
}

int TectonicPlates::rawPlateAt(double x, double z) const {
    return voronoi_layout::nearestSite(plates_, x, z, world_);
}

int TectonicPlates::boundaryIndex(int a, int b) const {
    const std::size_t count = plates_.size();
    return boundaryIndex_[static_cast<std::size_t>(a) * count + static_cast<std::size_t>(b)];
}

PlateSample TectonicPlates::sampleAt(double x, double z) const {
    return sampleWith(x, z, nullptr);
}

PlateSample TectonicPlates::sampleAt(double x, double z, Memos& memos) const {
    return sampleWith(x, z, &memos);
}

PlateSample TectonicPlates::sampleWith(double worldX, double worldZ, Memos* memos) const {
    const double x = noise::wrapCanonical(worldX, world_.x);
    const double z = noise::wrapCanonical(worldZ, world_.z);
    const Neighborhood hood = neighborhood(*this, warpWith(x, z, memos), gateWidth_);
    const Near& nearest = hood.near[0];
    PlateSample s;
    s.plate = nearest.plate;
    s.neighbor = hood.second.plate;
    s.boundaryDistance = bisectorDistance(nearest, hood.second);
    s.level = blendedLevel(*this, hood);
    addBoundaryEffects(*this, hood, s);
    if (s.coast > 0.0f && params_.coastRoughness > 0.0f) {
        const float rough = memos ? coast_.fbm2D(x, z, memos->coast) : coast_.fbm2D(x, z);
        s.level += params_.coastRoughness * s.coast * rough;
    }
    s.continentalness = s.level + params_.collisionUplift * s.collision + s.rift;
    return s;
}

} // namespace terrain
