#include "coast_shape.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::coast_shape {

namespace {

// The coastal plain rises this many meters per meter from the shore, up to kCoastPlainTop,
// before the regional elevation takes over over GeographyParams::coastRamp.
constexpr double kCoastPlainRise = 0.002;
constexpr double kCoastPlainTop = 20.0;

// Streams and wavelengths of the noises of the shapes.
constexpr std::uint64_t kHeightStream = 0x33B1;
constexpr std::uint64_t kPlatformStream = 0x33B2;
constexpr std::uint64_t kFaceStream = 0x33B3;
constexpr std::uint64_t kStretchStream = 0x33B4;
constexpr std::uint64_t kEdgeStream = 0x33B5;
constexpr std::uint64_t kScreeStream = 0x33B6;
constexpr double kStretchWavelength = 30'000.0;
constexpr double kHeightWavelength = 5'000.0;
constexpr double kPlatformWavelength = 1'700.0;
constexpr double kFaceWavelength = 900.0;
constexpr double kEdgeWavelength = 350.0;
constexpr double kScreeWavelength = 1'200.0;

// The slope law of the faces (faceSlopeLimit): the height of a face in cells of kFaceCell meters
// (the LOD 3 of the tiles), and the steepest slope drawn without teeth at each height in cells,
// each twice the one before (measured, coast_patterns_test).
constexpr double kFaceCell = 8.0;
constexpr std::array<double, 4> kCleanCells{2.5, 5.0, 10.0, 20.0};
constexpr std::array<double, 4> kCleanSlopes{0.75, 1.25, 2.0, 3.0};
// A face of width w rising h is at most kSmoothSlope h / w steep (a smoothstep), and the distance
// to the shore is that of the coast signal over its gradient, steeper by up to kDistanceSlack.
constexpr double kSmoothSlope = 1.5;
constexpr double kDistanceSlack = 1.25;

// The shape of the shore of a kind: the top of its face heightLow to heightHigh meters above its
// platform by the height noises, at most landShare of the regional elevation plus landTop (no
// plateau above its hinterland); a rock platform platformLow to platformHigh meters wide before
// a face as wide as the slope law wants plus faceLow to faceHigh meters; screes at the foot of
// its face along up to the share screes of its length; its mountains faded in over hills meters
// of signal. Out at sea its floor goes down, the share deepen of the way to the depth of the
// outer edge of the band within foot meters of the shore.
struct Profile {
    float heightLow;
    float heightHigh;
    float landShare;
    float landTop;
    float platformLow;
    float platformHigh;
    float faceLow;
    float faceHigh;
    float screes;
    double hills;
    float deepen;
    double foot;
};

// By kind, in the order of CoastKind (the low coasts keep the coastal plain and the shelf; the
// mountains and the fjords have no shore of their own: they plunge).
constexpr std::array<Profile, kCoastKindCount> kProfiles{{
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30'000.0, 0.0f, 1.0},
    {2.0f, 14.0f, 0.5f, 6.0f, 5.0f, 40.0f, 0.0f, 6.0f, 0.0f, 12'000.0, 0.5f, 300.0},
    {4.0f, 180.0f, 0.8f, 25.0f, 10.0f, 120.0f, 0.0f, 10.0f, 0.8f, 15'000.0, 0.8f, 200.0},
    {8.0f, 200.0f, 0.9f, 40.0f, 0.0f, 10.0f, 0.0f, 10.0f, 0.6f, 10'000.0, 1.0f, 100.0},
    {4.0f, 50.0f, 0.8f, 15.0f, 0.0f, 5.0f, 40.0f, 120.0f, 0.0f, 10'000.0, 0.5f, 300.0},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, 1.0f, 150.0},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, 1.0f, 120.0},
}};

// The height of the faces: the slow noise, contrasted from kStretchLow to kStretchHigh, times
// kMediumLow to 1 by the medium one: from a few meters to the highest along the same coast.
constexpr double kStretchLow = -0.45;
constexpr double kStretchHigh = 0.45;
constexpr double kMediumLow = 0.3;
// The rock platform rises from kBenchLow to kBenchLow + kBenchRise meters across its width (less
// on a platform narrower than kBenchRun: none where the face rises from the water), from the
// water over its first kBenchFoot meters; the foot of the face moves kEdgeShift meters to and fro
// along the coast (capes and hollows of the top).
constexpr double kBenchLow = 0.4;
constexpr double kBenchRise = 1.1;
constexpr double kBenchRun = 20.0;
constexpr double kBenchFoot = 6.0;
constexpr double kEdgeShift = 30.0;
// The screes: where their noise passes kScreeLow to kScreeHigh, an apron up to kScreeShare of the
// height of the face, ending kScreeEnd of the way up its width, at most kScreeSlope steep and
// never more than kScreeSteep of the face, blended into it over kScreeBlend of its height.
constexpr double kScreeLow = 0.45;
constexpr double kScreeHigh = 0.75;
constexpr double kScreeShare = 0.4;
constexpr double kScreeEnd = 0.45;
constexpr double kScreeSlope = 0.7;
constexpr double kScreeSteep = 0.5;
constexpr double kScreeBlend = 0.25;
// The screes never reach the water side of the platform: their toe stays kScreeKeep of its width
// from the water at least.
constexpr double kScreeKeep = 0.6;
// The hills and the vallons of the hinterland come onto the plateau over kPlateauRise meters
// beyond the top of the face.
constexpr double kPlateauRise = 250.0;
// The mountains plunging into the sea rise from the shore, steepest there, over kPlungeRamp
// meters of coast signal (about 5 km): read on the signal, smooth, not on the distance to the
// shore, whose rounding the heights of the mountains would show.
constexpr double kPlungeRamp = 15'000.0;
// Where the shore kinds weigh kHeldShare or more of the weight of the shore and plunging kinds,
// the plunging kinds keep off their platform.
constexpr double kHeldShare = 0.5;
// Every kind gives way to the low coast (the ground and the factors of the land out of the band)
// over the last kInnerFade meters of signal of the band, the last kInnerSlack excepted.
constexpr double kInnerFade = 10'000.0;
constexpr double kInnerSlack = 1'000.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double lerp(float low, float high, double t) {
    return static_cast<double>(low) + static_cast<double>(high - low) * t;
}

// 1 - (1 - t)^2 over [0, 1]: steepest at 0, flat at 1.
double easeOut(double t) {
    const double u = std::clamp(t, 0.0, 1.0);
    return u * (2.0 - u);
}

// The greater of a and b, rounded over k (polynomial smooth maximum).
double smoothMax(double a, double b, double k) {
    if (!(k > 0.0)) {
        return std::max(a, b);
    }
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::max(a, b) + 0.25 * k * h * h;
}

noise::FbmOctaves shapeNoise(std::uint64_t seed, std::uint64_t stream, double wavelength,
                             const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = 3, .frequency = 1.0 / wavelength, .gain = 0.5}, world);
}

// A noise in [0, 1].
double unit(const noise::FbmOctaves& n, double x, double z) {
    return std::clamp(0.5 + 0.5 * static_cast<double>(n.fbm2D(x, z)), 0.0, 1.0);
}

// The noises at a point of the shore, in [0, 1] (the edge in [-1, 1]).
struct Shore {
    double heightAt = 0.0;
    double platformAt = 0.0;
    double faceAt = 0.0;
    double edgeAt = 0.0;
    double screeAt = 0.0;
};

Shore shoreAt(const Noises& n, double x, double z) {
    const double stretch =
        smooth(kStretchLow, kStretchHigh, static_cast<double>(n.stretch.fbm2D(x, z)));
    return {stretch * (kMediumLow + (1.0 - kMediumLow) * unit(n.height, x, z)),
            smooth(0.2, 0.8, unit(n.platform, x, z)), unit(n.face, x, z),
            2.0 * smooth(0.2, 0.8, unit(n.edge, x, z)) - 1.0,
            smooth(kScreeLow, kScreeHigh, unit(n.scree, x, z))};
}

// The face of a shore: its foot, its width, the ground before it.
struct Face {
    double foot = 0.0;
    double width = 0.0;
    double bench = 0.0;
    double height = 0.0; // from the bench to the top
};

// The apron of screes at the foot of a face, at distance d from the water: rising as the square
// of the distance from its toe up to its top, kScreeEnd of the way up the face, at most
// kScreeSlope and kScreeSteep of the face steep there; smaller where the platform leaves no room
// (its toe never on the water side of the platform, which stays flat).
double screesAt(const Face& f, double patch, double d) {
    double top = patch * kScreeShare * f.height;
    const double slope =
        std::min(kScreeSlope, kScreeSteep * kSmoothSlope * f.height / f.width);
    if (!(top > 0.0) || !(slope > 0.0)) {
        return f.bench;
    }
    const double end = f.foot + kScreeEnd * f.width;
    double width = 2.0 * top / slope;
    const double toe = std::max(kBenchFoot, kScreeKeep * f.foot);
    if (end - width < toe) {
        const double room = std::max(0.0, end - toe);
        top *= (room / width) * (room / width);
        width = room;
    }
    if (!(width > 0.0)) {
        return f.bench;
    }
    const double u = std::clamp((d - (end - width)) / width, 0.0, 1.0);
    return f.bench + top * u * u;
}

// The face of a shore of profile k rising to top at distance d > 0 from the water, as wide as the
// slope law wants (faceSlopeLimit), its foot moved by the platform and the edge.
Face faceAt(const Profile& k, double top, double d, const Shore& s) {
    Face f;
    f.foot = std::max(0.0, lerp(k.platformLow, k.platformHigh, s.platformAt) +
                               kEdgeShift * s.edgeAt);
    const double rise = kBenchRise * std::min(d, f.foot) / std::max(f.foot, kBenchRun);
    f.bench = (kBenchLow + rise) * smooth(0.0, kBenchFoot, d);
    f.height = std::max(0.0, top - f.bench);
    f.width = kDistanceSlack * kSmoothSlope * f.height / faceSlopeLimit(f.height) +
              lerp(k.faceLow, k.faceHigh, s.faceAt);
    f.width = std::max(f.width, kBenchFoot);
    return f;
}

// The factors of the land and the ground of a kind on land (coast > 0), before the inner edge of
// the band.
struct Land {
    double land = 0.0;
    double mountains = 0.0;
    double ground = 0.0;
    double up = 1.0; // the shore kinds: how far up their face, 0 on the platform, 1 on the top
};

// The rocky coasts, the cliffs, the calanques and the rias: the platform, the screes and the face
// up to a plateau as high as the height noises want, its hills and vallons coming onto it beyond
// the top of the face, its mountains later.
Land shoreOf(const Profile& k, const GeographySample& g, double coast, double d, const Shore& s,
             double old) {
    const double high = lerp(k.heightLow, k.heightHigh, s.heightAt);
    const double cap = static_cast<double>(k.landShare) *
                           std::max(0.0, static_cast<double>(g.land)) +
                       static_cast<double>(k.landTop);
    const Face f = faceAt(k, std::min(high, cap), d, s);
    const double face = f.bench + f.height * smooth(f.foot, f.foot + f.width, d);
    const double patch = static_cast<double>(k.screes) * s.screeAt;
    // Rounded where the screes meet the face, never more than they stand above the platform: the
    // platform before them keeps its height.
    const double screes = patch > 0.0 ? screesAt(f, patch, d) : f.bench;
    const double round = std::min(kScreeBlend * patch * f.height, screes - f.bench);
    const double ground = round > 0.0 ? smoothMax(face, screes, round) : std::max(face, screes);
    const double top = f.foot + f.width;
    const double land = smooth(top, top + kPlateauRise, d);
    return {land, land * smooth(0.0, k.hills, coast), std::max(old, ground),
            smooth(f.foot, top, d)};
}

// The mountains and the fjords: the mountains and the regional elevation rise from the shore,
// steepest there (no ledge at the water), held down by held (in [0, 1]) where they share the coast
// with cliffs.
Land plungeOf(const GeographySample& g, double coast, double held) {
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double rise = easeOut(coast / kPlungeRamp) * held;
    return {rise, rise, plain + (static_cast<double>(g.land) - plain) * rise};
}

bool isShore(CoastKind kind) {
    return kind == CoastKind::Rocky || kind == CoastKind::Cliff || kind == CoastKind::Calanque ||
           kind == CoastKind::Ria;
}

bool plunges(CoastKind kind) {
    return kind == CoastKind::Mountain || kind == CoastKind::Fjord;
}

// The land of the kinds blended by their weights: the shore kinds first; where they weigh more
// than the plunging kinds (from kHeldShare of the weight of both on), the plunging kinds rise only
// up the face of the shore kinds, leaving their platform flat; the low coast as without the
// coasts.
Land blendLand(const coast_kinds::Weights& weights, const GeographySample& g, double coast,
               double d, const Shore& s, double oldLand, double old) {
    Land sum;
    double shoreWeight = 0.0;
    double up = 0.0;
    double plungeWeight = 0.0;
    const auto add = [&sum](double w, const Land& l) {
        sum.land += w * l.land;
        sum.mountains += w * l.mountains;
        sum.ground += w * l.ground;
    };
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto kind = static_cast<CoastKind>(k);
        const auto w = static_cast<double>(weights[k]);
        plungeWeight += plunges(kind) ? w : 0.0;
        if (isShore(kind) && w > 0.0) {
            const Land l = shoreOf(kProfiles[k], g, coast, d, s, old);
            add(w, l);
            shoreWeight += w;
            up += w * l.up;
        }
    }
    const double share = shoreWeight > 0.0 ? shoreWeight / (shoreWeight + plungeWeight) : 0.0;
    const double held =
        shoreWeight > 0.0 ? 1.0 - smooth(0.0, kHeldShare, share) * (1.0 - up / shoreWeight) : 1.0;
    for (std::size_t k = 0; k < weights.size(); ++k) {
        const auto kind = static_cast<CoastKind>(k);
        if (weights[k] > 0.0f && plunges(kind)) {
            add(static_cast<double>(weights[k]), plungeOf(g, coast, held));
        }
    }
    add(static_cast<double>(weights[static_cast<std::size_t>(CoastKind::Low)]),
        {oldLand, oldLand, old});
    return sum;
}

// The floor of a kind at sea (coast < 0): from the shelf at the shore, the share deepen of the way
// down to the shelf at the outer edge of the band within foot meters (steepest at the shore),
// never below it: the floor only goes down out to sea, and meets the shelf there (no ridge along
// the coast, no ledge).
double seaOf(const GeographyParams& p, CoastKind kind, const GeographySample& g, double coast,
             double d) {
    if (kind == CoastKind::Low) {
        return static_cast<double>(groundBase(p, g, coast));
    }
    const Profile& k = kProfiles[static_cast<std::size_t>(kind)];
    const double edge = shelf(p, -kCoastSeaBand);
    const double down = static_cast<double>(k.deepen) * easeOut(-d / k.foot);
    const double floor = edge + (shelf(p, coast) - edge) * (1.0 - down);
    return std::max(static_cast<double>(g.sea), floor);
}

} // namespace

double faceSlopeLimit(double height) {
    const double cells = height / kFaceCell;
    if (!(cells > kCleanCells.front())) {
        return kCleanSlopes.front();
    }
    for (std::size_t i = 1; i < kCleanCells.size(); ++i) {
        if (cells < kCleanCells[i]) {
            const double t = dmath::log2(cells / kCleanCells[i - 1]);
            return kCleanSlopes[i - 1] + (kCleanSlopes[i] - kCleanSlopes[i - 1]) * t;
        }
    }
    return kCleanSlopes.back();
}

bool inBand(const GeographyParams& p, double coast) {
    return coast > -kCoastSeaBand && coast < p.coastRamp;
}

float landFactor(const GeographyParams& p, double coast) {
    return noise::smoothstep(0.0f, static_cast<float>(p.coastRamp), static_cast<float>(coast));
}

double shelf(const GeographyParams& p, double coast) {
    const double d = -coast;
    const double depth = static_cast<double>(p.shelfDepth);
    return d <= p.shelfWidth ? depth * d / p.shelfWidth
                             : depth - (d - p.shelfWidth) * p.shelfSlope;
}

float groundBase(const GeographyParams& p, const GeographySample& g, double coast) {
    if (coast < 0.0) {
        return std::max(g.sea, static_cast<float>(shelf(p, coast)));
    }
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double t = landFactor(p, coast);
    return static_cast<float>(plain + (static_cast<double>(g.land) - plain) * t);
}

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    return {shapeNoise(seed, kStretchStream, kStretchWavelength, world),
            shapeNoise(seed, kHeightStream, kHeightWavelength, world),
            shapeNoise(seed, kPlatformStream, kPlatformWavelength, world),
            shapeNoise(seed, kFaceStream, kFaceWavelength, world),
            shapeNoise(seed, kEdgeStream, kEdgeWavelength, world),
            shapeNoise(seed, kScreeStream, kScreeWavelength, world)};
}

Ground shapeAt(const GeographyParams& p, const Noises& noises, double x, double z,
               const GeographySample& g, double coast, double distance,
               const coast_kinds::Weights& weights) {
    if (coast < 0.0) {
        double ground = 0.0;
        for (std::size_t k = 0; k < weights.size(); ++k) {
            if (weights[k] > 0.0f) {
                ground += static_cast<double>(weights[k]) *
                          seaOf(p, static_cast<CoastKind>(k), g, coast, distance);
            }
        }
        return {0.0f, 0.0f, static_cast<float>(ground), 1.0f, 0.0f};
    }
    bool shores = false;
    for (const CoastKind kind :
         {CoastKind::Rocky, CoastKind::Cliff, CoastKind::Calanque, CoastKind::Ria}) {
        shores = shores || weights[static_cast<std::size_t>(kind)] > 0.0f;
    }
    const Shore shore = shores ? shoreAt(noises, x, z) : Shore{};
    const double oldLand = static_cast<double>(landFactor(p, coast));
    const double old = static_cast<double>(groundBase(p, g, coast));
    const Land sum = blendLand(weights, g, coast, distance, shore, oldLand, old);
    const double inner =
        smooth(p.coastRamp - kInnerFade, p.coastRamp - kInnerSlack, coast);
    const auto land = static_cast<float>(sum.land + (oldLand - sum.land) * inner);
    const float low = weights[static_cast<std::size_t>(CoastKind::Low)];
    return {land, static_cast<float>(sum.mountains + (oldLand - sum.mountains) * inner),
            static_cast<float>(sum.ground + (old - sum.ground) * inner),
            low + (1.0f - low) * land, static_cast<float>((1.0 - low) * (1.0 - inner))};
}

} // namespace terrain::coast_shape
