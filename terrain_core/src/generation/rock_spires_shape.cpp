#include "rock_spires.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

// The shapes of the spires drawn from their seeds (rock_spires.hpp): their kinds, the faces of
// their pillars, their beds, their blocks and the rest of their details.
namespace terrain::rock_spires {

namespace {

constexpr std::uint64_t kShapeSalt = 0x5B12E5ULL;
// The draws of the cracks, of the faces of the pillars, of the beds and of the blocks, past those
// of the rest of the shape (shapeOf, drawPillars).
constexpr std::uint64_t kCrackDraws = 70;
constexpr std::uint64_t kFaceDraws = 100;
constexpr std::uint64_t kBlockDraws = 200;
// The kinds by their share: pillars below the first bound, needles, splits, arches above the last.
constexpr std::array<double, 3> kKindBounds{0.4, 0.65, 0.85};
// The faces of a pillar turn from one to the next by their mean turn, kGapLow to kGapHigh of it,
// never within kRightOff turns of a quarter turn (no right angle).
constexpr double kGapLow = 0.6;
constexpr double kGapHigh = 1.4;
constexpr double kRightOff = 1.0 / 36.0;
// A bed recedes by up to its jitter, or stands out by up to kBedOut of it (a ledge under most
// beds, an overhang under a few: rock_spires.cpp keeps them within its height), each face
// kFaceJitter of it more or less.
constexpr double kBedOut = 0.35;
constexpr double kFaceJitter = 0.35;
// The beds begin kBedsOver meters over the notch.
constexpr double kBedsOver = 1.0;
// A block stands kBlockRoom meters off the foot at least (no gap a walker would stick in).
constexpr double kBlockRoom = 1.5;

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

// A draw in [0, 1) of a seed.
double drawOf(std::uint64_t seed, std::uint64_t k) {
    return static_cast<double>(noise::deriveSeed(seed, k) >> 11U) * 0x1.0p-53;
}

// The faces of a pillar: least to most of them, turning around it by uneven gaps; those facing
// along axis (turns) the further its stretch (its angles kept: no right angle made by it).
void drawFaces(Pillar& p, std::uint64_t seed, int least, int most, double axis) {
    const auto d = [seed](std::uint64_t k) { return drawOf(seed, k); };
    const double count = static_cast<double>(most - least + 1) * d(0);
    p.sides = std::min(most, least + static_cast<int>(count));
    std::array<double, kMaxSides> gap{};
    double sum = 0.0;
    for (int i = 0; i < p.sides; ++i) {
        const auto j = static_cast<std::size_t>(i);
        gap[j] = between(kGapLow, kGapHigh, d(1 + static_cast<std::uint64_t>(i)));
        sum += gap[j];
    }
    std::size_t widest = 0;
    for (int i = 0; i < p.sides; ++i) {
        const auto j = static_cast<std::size_t>(i);
        gap[j] /= sum;
        widest = gap[j] > gap[widest] ? j : widest;
    }
    // A gap of a quarter turn is a right angle: kRightOff away from it, the widest gap paying.
    double moved = 0.0;
    for (int i = 0; i < p.sides; ++i) {
        double& g = gap[static_cast<std::size_t>(i)];
        if (static_cast<std::size_t>(i) != widest && std::abs(g - 0.25) < kRightOff) {
            const double to = g < 0.25 ? 0.25 - kRightOff : 0.25 + kRightOff;
            moved += to - g;
            g = to;
        }
    }
    gap[widest] -= moved;
    if (std::abs(gap[widest] - 0.25) < kRightOff) {
        gap[widest] = gap[widest] < 0.25 ? 0.25 - kRightOff : 0.25 + kRightOff;
    }
    double turns = d(10);
    for (int i = 0; i < p.sides; ++i) {
        const auto j = static_cast<std::size_t>(i);
        const dmath::SineCosine n = dmath::sinCosTurns(turns);
        p.faceX[j] = n.cosine;
        p.faceZ[j] = n.sine;
        const double along = dmath::cosTurns(turns - axis);
        p.faceReach[j] = between(0.9, 1.1, d(11 + static_cast<std::uint64_t>(i))) *
                         (1.0 + p.stretch * along * along);
        turns += gap[j];
    }
}

void drawPillars(Shape& s, std::uint64_t h, double radius, double height) {
    const auto d = [h](std::uint64_t k) { return drawOf(h, k); };
    switch (s.kind) {
    case Kind::Pillar:
        s.pillars[0] = {0.0, 0.0, 0.62 * radius, height, between(0.28, 0.42, d(30)),
                        between(1.0, 1.4, d(31)), between(0.05, 0.15, d(32))};
        break;
    case Kind::Needle:
        s.pillars[0] = {0.0, 0.0, 0.5 * radius, height * between(1.0, 1.15, d(30)),
                        between(0.55, 0.7, d(31)), between(0.7, 0.9, d(32)), 0.05};
        break;
    case Kind::Split: {
        const double base = 0.62 * radius;
        const double a = 0.45 * base * between(0.9, 1.1, d(30));
        const double b = 0.45 * base * between(0.9, 1.1, d(31));
        const double off = 0.5 * (a + b + 0.12 * base);
        const dmath::SineCosine axis = dmath::sinCosTurns(s.axis);
        const double tall = d(32) < 0.5 ? 1.0 : between(0.6, 0.9, d(33));
        s.pillars[0] = {0.0, 0.0, base, height * between(0.2, 0.35, d(34)), 0.08, 1.0, 0.1};
        s.pillars[1] = {off * axis.cosine, off * axis.sine, a, height * tall,
                        between(0.15, 0.3, d(35)), 1.2, 0.0};
        s.pillars[2] = {-off * axis.cosine, -off * axis.sine, b,
                        height * (tall < 1.0 ? 1.0 : between(0.6, 0.9, d(36))),
                        between(0.15, 0.3, d(37)), 1.2, 0.0};
        s.pillarCount = 3;
        break;
    }
    case Kind::Arch:
        s.pillars[0] = {0.0, 0.0, 0.55 * radius, height, between(0.4, 0.5, d(30)), 1.2, 0.35};
        s.archHalf = between(0.28, 0.36, d(31)) * 1.35 * s.pillars[0].radius;
        s.archHeight = between(0.35, 0.5, d(32)) * height;
        break;
    case Kind::Block:
        s.pillars[0] = {0.0, 0.0, between(0.25, 0.4, d(30)) * radius, height,
                        between(0.05, 0.2, d(31)), 1.0, between(0.0, 0.3, d(32))};
        break;
    }
    for (int i = 0; i < s.pillarCount; ++i) {
        const bool small = (s.kind == Kind::Split && i > 0) || s.kind == Kind::Block;
        drawFaces(s.pillars[static_cast<std::size_t>(i)],
                  noise::deriveSeed(h, kFaceDraws + static_cast<std::uint64_t>(i)), small ? 4 : 5,
                  small ? 6 : 7, s.axis);
    }
}

// Its beds, from over its notch up to its top, of their own thicknesses (bed meters on average),
// each standing out or receding (mostly) by its own draw, each of its faces by its own.
void drawBeds(Shape& s, std::uint64_t seed, double bed, double jitter, double top) {
    const auto d = [seed](std::uint64_t k) { return drawOf(seed, k); };
    double y = s.notchTop + kBedsOver;
    s.bedBottom[0] = y;
    int k = 0;
    while (k < kMaxBeds && y < top) {
        const auto j = static_cast<std::size_t>(k);
        const std::uint64_t at = 16 * static_cast<std::uint64_t>(k);
        y += bed * between(0.6, 1.4, d(at));
        s.bedBottom[j + 1] = k + 1 < kMaxBeds ? y : std::max(y, top);
        s.bedOut[j] = jitter * ((1.0 + kBedOut) * d(at + 1) - 1.0);
        for (std::size_t f = 0; f < static_cast<std::size_t>(kMaxSides); ++f) {
            s.bedFace[j][f] = kFaceJitter * jitter * (2.0 * d(at + 2 + f) - 1.0);
        }
        ++k;
    }
    s.bedCount = k;
}

// Its blocks: fallen at its foot (kBlockRoom off it at least), most on its platform, a few in the
// sea around it.
void drawBlocks(Shape& s, std::uint64_t h, double radius, double height) {
    const bool block = s.kind == Kind::Block;
    s.blockCount = block ? static_cast<int>(4.0 * drawOf(h, 54))
                         : 5 + static_cast<int>(6.0 * drawOf(h, 54));
    for (int i = 0; i < s.blockCount; ++i) {
        const std::uint64_t seed =
            noise::deriveSeed(h, kBlockDraws + static_cast<std::uint64_t>(i));
        const auto d = [seed](std::uint64_t k) { return drawOf(seed, k); };
        const bool platform = d(0) < 0.7;
        const double turns = platform ? s.apronTurns + between(-0.3, 0.3, d(1)) : d(1);
        const dmath::SineCosine toward = dmath::sinCosTurns(turns);
        const double half = (block ? between(0.6, 1.8, d(2)) : between(2.0, 4.5, d(2))) *
                            std::min(1.0, radius / 25.0);
        const double out = kBlockRoom + (platform ? between(0.0, 0.8, d(3)) * s.apronReach
                                                  : between(0.0, 3.5, d(3)));
        const double away = footprintAt(s, turns) + half + out;
        s.blocks[static_cast<std::size_t>(i)] = {away * toward.cosine,
                                                 away * toward.sine,
                                                 half,
                                                 half * between(0.6, 1.0, d(4)),
                                                 std::min(0.1 * height, between(1.5, 5.0, d(5))),
                                                 d(6),
                                                 between(-0.25, 0.25, d(7)),
                                                 between(-0.25, 0.25, d(8))};
    }
}

// The rest of the shape: its cracks, its lean, its top, its platform.
void drawDetails(Shape& s, std::uint64_t h, double radius, double height) {
    const auto d = [h](std::uint64_t k) { return drawOf(h, k); };
    double narrowest = s.pillars[0].radius;
    for (int i = 1; i < s.pillarCount; ++i) {
        narrowest = std::min(narrowest, s.pillars[static_cast<std::size_t>(i)].radius);
    }
    const bool block = s.kind == Kind::Block;
    const int more = std::min(s.kind == Kind::Arch ? 0 : 2, static_cast<int>(3.0 * d(19)));
    s.crackCount = block ? (d(19) < 0.4 ? 1 : 0) : 1 + more;
    for (std::size_t c = 0; c < static_cast<std::size_t>(s.crackCount); ++c) {
        const std::uint64_t k = kCrackDraws + 8 * c; // the draws of the crack
        const double low = between(0.05, 0.45, d(k + 3)) * height;
        s.cracks[c] = {d(k),
                       between(1.5, 3.0, d(k + 1)),
                       std::min(0.25 * narrowest, between(2.0, 5.0, d(k + 2))),
                       low,
                       low + between(0.3, 0.6, d(k + 4)) * height,
                       between(0.01, 0.03, d(k + 5)),
                       d(k + 6)};
    }
    const dmath::SineCosine lean = dmath::sinCosTurns(d(49));
    const double leaning =
        block ? 0.0 : between(0.01, 0.04, d(50)) * (s.kind == Kind::Needle ? 1.5 : 1.0);
    s.leanX = leaning * lean.cosine;
    s.leanZ = leaning * lean.sine;
    s.topTilt = block ? between(0.04, 0.12, d(20)) : between(0.005, 0.025, d(20));
    s.tiltTurns = d(21);
    if ((s.kind == Kind::Pillar || s.kind == Kind::Arch || block) && d(22) < 0.45) {
        s.topStep = (block ? between(0.15, 0.3, d(23)) : between(0.06, 0.15, d(23))) * height;
        s.stepTurns = d(24);
        s.stepAt = between(-0.2, 0.4, d(25));
    }
    s.apron = block ? between(0.3, 0.8, d(52)) : between(0.6, 1.5, d(52));
    s.apronTurns = d(51);
    s.apronReach = between(0.2, 0.45, d(53)) * radius;
}

// What its density reads at every point, drawn once: the cosines and sines of its turns, the taper
// of its pillars at the bottom of each bed.
void precompute(Shape& s) {
    const auto set = [](double turns, double& x, double& z) {
        const dmath::SineCosine sc = dmath::sinCosTurns(turns);
        x = sc.cosine;
        z = sc.sine;
    };
    set(s.axis, s.axisX, s.axisZ);
    set(s.tiltTurns, s.tiltX, s.tiltZ);
    set(s.stepTurns, s.stepX, s.stepZ);
    for (int i = 0; i < s.blockCount; ++i) {
        Block& b = s.blocks[static_cast<std::size_t>(i)];
        set(b.turns, b.turnX, b.turnZ);
    }
    for (int i = 0; i < s.pillarCount; ++i) {
        Pillar& p = s.pillars[static_cast<std::size_t>(i)];
        for (int k = 0; k <= s.bedCount; ++k) {
            p.shrink[static_cast<std::size_t>(k)] =
                taperOf(p, s.bedBottom[static_cast<std::size_t>(k)]);
        }
    }
}

} // namespace

Shape shapeOf(std::uint32_t seed, double radius, double height) {
    Shape s;
    const std::uint64_t h = noise::deriveSeed(kShapeSalt, seed);
    const auto d = [h](std::uint64_t k) { return drawOf(h, k); };
    const double kind = d(0);
    s.kind = height < kStackLeast    ? Kind::Block
             : kind < kKindBounds[0] ? Kind::Pillar
             : kind < kKindBounds[1] ? Kind::Needle
             : kind < kKindBounds[2] ? Kind::Split
                                     : Kind::Arch;
    s.axis = d(1);
    const bool block = s.kind == Kind::Block;
    s.notchDepth = block ? between(0.8, 2.0, d(17)) : between(2.5, 4.5, d(17));
    s.notchTop =
        block ? std::min(between(1.0, 2.0, d(18)), 0.4 * height) : between(3.0, 5.0, d(18));
    s.lamina = between(1.0, 2.0, d(15));
    s.laminaDepth = between(0.15, 0.35, d(16));
    drawPillars(s, h, radius, height);
    drawDetails(s, h, radius, height);
    const double jitter = block ? between(0.4, 1.0, d(14)) : between(1.5, 3.0, d(14));
    const double bed = block ? between(2.0, 4.0, d(12))
                             : between(5.0, 9.0, d(12)) * (s.kind == Kind::Needle ? 0.8 : 1.0);
    drawBeds(s, noise::deriveSeed(h, 13), bed, jitter, summitOf(s));
    drawBlocks(s, h, radius, height);
    precompute(s);
    for (int i = 0; i < s.pillarCount; ++i) {
        const Pillar& p = s.pillars[static_cast<std::size_t>(i)];
        double reach = 0.0;
        for (int j = 0; j < p.sides; ++j) {
            reach = std::max(reach, p.faceReach[static_cast<std::size_t>(j)]);
        }
        // The farthest corner of its faces: within 1.6 of their reach (gaps under 101 degrees).
        const double lean = dmath::hypot(s.leanX, s.leanZ) * summitOf(s);
        s.reach = std::max(s.reach, dmath::hypot(p.x, p.z) + lean + 1.6 * p.radius * reach +
                                        kBedOut * jitter);
    }
    for (int i = 0; i < s.blockCount; ++i) {
        const Block& b = s.blocks[static_cast<std::size_t>(i)];
        s.reach = std::max(s.reach, dmath::hypot(b.x, b.z) + dmath::hypot(b.halfX, b.halfZ));
    }
    return s;
}

} // namespace terrain::rock_spires
