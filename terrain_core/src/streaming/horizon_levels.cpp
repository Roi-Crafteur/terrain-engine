#include "terrain/streaming/horizon_levels.hpp"

#include "terrain/chunk_coord.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terrain {

namespace {

bool powerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }

std::int64_t floorMod(std::int64_t value, std::int64_t modulus) {
    const std::int64_t rest = value % modulus;
    return rest < 0 ? rest + modulus : rest;
}

// The even sample nearest to v along an axis of the given period, canonical.
std::int64_t centerAlong(double v, double period, int pitch, std::int64_t count) {
    const double canonical = wrapCoordinate(v, period);
    // 2 * pitch is a power of two: the division is exact.
    const double pairs = std::floor(canonical / (2.0 * pitch) + 0.5);
    return wrapIndex(2 * static_cast<std::int64_t>(pairs), count);
}

// Pieces of [first, last) brought into [0, side): at most two, the second one starting at 0.
struct SlotSpan {
    std::int64_t first;
    std::int64_t last;
};

std::vector<SlotSpan> slotSpans(std::int64_t first, std::int64_t last, int side) {
    const std::int64_t start = floorMod(first, side);
    const std::int64_t end = start + (last - first);
    if (end <= side) {
        return {{start, end}};
    }
    return {{start, side}, {0, end - side}};
}

} // namespace

void validate(const HorizonParams& params) {
    validate(params.world);
    if (params.levels < 1 || params.levels > kHorizonMaxLevels) {
        throw std::invalid_argument("HorizonParams: levels must be within 1 and " +
                                    std::to_string(kHorizonMaxLevels));
    }
    if (params.cellsPerSide < 16 || params.cellsPerSide % 4 != 0) {
        throw std::invalid_argument("HorizonParams: cellsPerSide must be a multiple of 4 of at "
                                    "least 16");
    }
    if (!powerOfTwo(params.basePitch) ||
        (params.materialSlopeStep != 0 && !powerOfTwo(params.materialSlopeStep))) {
        throw std::invalid_argument("HorizonParams: the pitches must be powers of two");
    }
    // The coarsest pitch, as a double: no overflow for any level count.
    const double coarsest = std::ldexp(static_cast<double>(params.basePitch), params.levels - 1);
    if (kWorldAlignmentMeters / (2.0 * coarsest) < 1.0) {
        throw std::invalid_argument("HorizonParams: the coarsest pitch must divide half of the "
                                    "world alignment");
    }
    const double span = (params.cellsPerSide + 1) * coarsest;
    if (span > params.world.x || span > params.world.z) {
        throw std::invalid_argument("HorizonParams: the coarsest window is wider than the world");
    }
}

void validate(const HorizonParams& params, const LodParams& lod) {
    validate(params);
    validate(lod);
    // The tiles reach (viewDistance + tileChunks(kMaxLod)) chunks from the observer (the chunk
    // of the observer included); level 0 covers levelRadius around it.
    const double tiles = (lod.viewDistance + tileChunks(kMaxLod)) * kChunkSizeMeters;
    if (tiles + params.basePitch > levelRadius(params, 0)) {
        throw std::invalid_argument("HorizonParams: level 0 does not reach beyond the tiles");
    }
}

int levelPitch(const HorizonParams& params, int level) { return params.basePitch << level; }

int samplesPerSide(const HorizonParams& params) { return params.cellsPerSide + 1; }

double levelRadius(const HorizonParams& params, int level) {
    return (params.cellsPerSide / 2 - 1) * static_cast<double>(levelPitch(params, level));
}

double guaranteedRadius(const HorizonParams& params) {
    return levelRadius(params, params.levels - 1);
}

HorizonRect intersection(const HorizonRect& a, const HorizonRect& b) {
    return {std::max(a.x0, b.x0), std::max(a.z0, b.z0), std::min(a.x1, b.x1), std::min(a.z1, b.z1)};
}

HorizonIndex levelCounts(const HorizonParams& params, int level) {
    const int pitch = levelPitch(params, level);
    return {static_cast<std::int64_t>(params.world.x) / pitch,
            static_cast<std::int64_t>(params.world.z) / pitch};
}

HorizonIndex canonicalSample(HorizonIndex sample, HorizonIndex counts) {
    return {wrapIndex(sample.x, counts.x), wrapIndex(sample.z, counts.z)};
}

HorizonIndex canonicalCenter(const HorizonParams& params, int level, double x, double z) {
    if (!std::isfinite(x) || !std::isfinite(z)) {
        throw std::invalid_argument("canonicalCenter: the observer position must be finite");
    }
    const int pitch = levelPitch(params, level);
    const HorizonIndex counts = levelCounts(params, level);
    return {centerAlong(x, params.world.x, pitch, counts.x),
            centerAlong(z, params.world.z, pitch, counts.z)};
}

HorizonIndex moveCenter(HorizonIndex current, HorizonIndex target, HorizonIndex counts) {
    return {current.x + wrapIndex(target.x - wrapIndex(current.x, counts.x), counts.x),
            current.z + wrapIndex(target.z - wrapIndex(current.z, counts.z), counts.z)};
}

HorizonRect windowAround(HorizonIndex center, int cellsPerSide) {
    const std::int64_t half = cellsPerSide / 2;
    return {center.x - half, center.z - half, center.x + half + 1, center.z + half + 1};
}

std::vector<HorizonRect> newSamples(const HorizonRect& from, const HorizonRect& to) {
    if (to.empty()) {
        return {};
    }
    const HorizonRect common = intersection(from, to);
    if (common.empty()) {
        return {to};
    }
    std::vector<HorizonRect> rects;
    if (to.x0 < common.x0) {
        rects.push_back({to.x0, to.z0, common.x0, to.z1});
    }
    if (common.x1 < to.x1) {
        rects.push_back({common.x1, to.z0, to.x1, to.z1});
    }
    if (to.z0 < common.z0) {
        rects.push_back({common.x0, to.z0, common.x1, common.z0});
    }
    if (common.z1 < to.z1) {
        rects.push_back({common.x0, common.z1, common.x1, to.z1});
    }
    return rects;
}

HorizonIndex slotOf(HorizonIndex sample, int side) {
    return {floorMod(sample.x, side), floorMod(sample.z, side)};
}

std::vector<HorizonRect> slotRects(const HorizonRect& rect, int side) {
    if (rect.empty()) {
        return {};
    }
    if (rect.width() > side || rect.height() > side) {
        throw std::invalid_argument("slotRects: the rectangle is wider than the buffer");
    }
    std::vector<HorizonRect> rects;
    for (const SlotSpan& z : slotSpans(rect.z0, rect.z1, side)) {
        for (const SlotSpan& x : slotSpans(rect.x0, rect.x1, side)) {
            rects.push_back({x.first, z.first, x.last, z.last});
        }
    }
    return rects;
}

} // namespace terrain
