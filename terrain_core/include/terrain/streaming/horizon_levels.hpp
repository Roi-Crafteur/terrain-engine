#pragma once

#include "terrain/streaming/lod.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// Grids of the far horizon (phase 13): nested square grids of samples around an observer, the
// levels of a clipmap a renderer draws up to the horizon, hundreds of kilometers away.
//
// Level n samples the world every basePitch * 2^n meters: sample (i, k) lies at (i, k) * pitch.
// Its window holds (K + 1) x (K + 1) samples (K = cellsPerSide cells) centered on an even sample
// near the observer, so that both of its borders lie on samples of the next coarser level. The
// default levels go from 8 m, the lattice of the coarsest tiles (tileCellMeters(kMaxLod)), to
// 4096 m: level 0 meets the tiles sample for sample, and the coarsest one reaches 520 km.
//
// The world wraps around (WorldSize). The samples are addressed by unwrapped indices that stay
// continuous along the path of the observer (moveCenter): crossing an edge of the world moves a
// window by a few samples, never by a whole world. The canonical index of a sample (wrapIndex
// on each axis) identifies it: its values are computed at its canonical position, so they are
// the same bits for every image and for every observer.
inline constexpr int kHorizonMaxLevels = 16;
inline constexpr int kHorizonCrestRatioMax = 16;

struct HorizonParams {
    int levels = 10;
    int cellsPerSide = 256; // K, a multiple of 4
    // Pitch of level 0, meters, a power of two: by default the lattice of the coarsest tiles.
    int basePitch = static_cast<int>(tileCellMeters(kMaxLod));
    // Meters between the ground samples whose slope classifies the material of a sample (as
    // surfaceFromHeightfield at that spacing), a power of two: the material of a point is then
    // the same on every level. 0: the pitch of each level (cheaper, coarser slopes far away).
    int materialSlopeStep = static_cast<int>(tileCellMeters(kMaxLod));
    // Crest prefilter (horizon_crests.hpp): from level 1 on, the samples rise to the crests of
    // their cells, sought on a lattice crestRatio times finer than the level. 0: every sample is
    // the point height of the ground (the horizon before the prefilter).
    int crestRatio = 4;
    WorldSize world;
};

// Throws std::invalid_argument unless the world is valid, 1 <= levels <= kHorizonMaxLevels,
// cellsPerSide is a multiple of 4 of at least 16, basePitch and materialSlopeStep (unless 0) are
// powers of two, crestRatio is 0 or a power of two from 2 to kHorizonCrestRatioMax dividing the
// pitch of level 1 (2 * basePitch), twice the coarsest pitch divides kWorldAlignmentMeters (an
// even number of samples along each side of the world) and the window of the coarsest level
// holds no sample twice ((K + 1) * pitch <= each side of the world).
void validate(const HorizonParams& params);

// As validate, and level 0 reaches beyond every tile the LOD parameters select around the
// observer, by one cell: the tiles end inside level 0, where their borders meet its samples.
void validate(const HorizonParams& params, const LodParams& lod);

// Pitch of a level, in meters, and the samples along each side of its window (K + 1).
[[nodiscard]] int levelPitch(const HorizonParams& params, int level);
[[nodiscard]] int samplesPerSide(const HorizonParams& params);

// Radius around the observer covered by every level up to this one, whatever its position:
// (K / 2 - 1) * pitch, the snapping of the window to even samples taking one pitch.
[[nodiscard]] double levelRadius(const HorizonParams& params, int level);
[[nodiscard]] double guaranteedRadius(const HorizonParams& params); // of the coarsest level

// Index of a sample of a level along x and z.
struct HorizonIndex {
    std::int64_t x = 0;
    std::int64_t z = 0;

    friend bool operator==(const HorizonIndex&, const HorizonIndex&) = default;
};

// Samples [x0, x1) x [z0, z1) of a level.
struct HorizonRect {
    std::int64_t x0 = 0;
    std::int64_t z0 = 0;
    std::int64_t x1 = 0;
    std::int64_t z1 = 0;

    [[nodiscard]] bool empty() const { return x1 <= x0 || z1 <= z0; }
    [[nodiscard]] std::int64_t width() const { return empty() ? 0 : x1 - x0; }
    [[nodiscard]] std::int64_t height() const { return empty() ? 0 : z1 - z0; }
    [[nodiscard]] std::int64_t count() const { return width() * height(); }
    [[nodiscard]] bool contains(HorizonIndex sample) const {
        return sample.x >= x0 && sample.x < x1 && sample.z >= z0 && sample.z < z1;
    }

    friend bool operator==(const HorizonRect&, const HorizonRect&) = default;
};

[[nodiscard]] HorizonRect intersection(const HorizonRect& a, const HorizonRect& b);

// Samples of a level along each side of the world: its sides divided by the pitch (even).
[[nodiscard]] HorizonIndex levelCounts(const HorizonParams& params, int level);

// The sample brought back into the canonical range of its level ([-count / 2, count / 2) on
// each axis): the same index for every image.
[[nodiscard]] HorizonIndex canonicalSample(HorizonIndex sample, HorizonIndex counts);

// World position of a sample along one axis, meters (exact).
[[nodiscard]] inline double samplePosition(std::int64_t index, int pitch) {
    return static_cast<double>(index) * pitch;
}

// Canonical index of the window center of a level for an observer at (x, z), any image: the
// even sample nearest to the observer, 2 * floor(x / (2 pitch) + 1/2) on its canonical
// position. Throws std::invalid_argument for a non-finite position.
[[nodiscard]] HorizonIndex canonicalCenter(const HorizonParams& params, int level, double x,
                                           double z);

// The center moved to the image of target (canonical) nearest to it: current plus the shortest
// move on the wrapped world, along each axis.
[[nodiscard]] HorizonIndex moveCenter(HorizonIndex current, HorizonIndex target,
                                      HorizonIndex counts);

// Window of the samples around a center: [center - K / 2, center + K / 2] on each axis.
[[nodiscard]] HorizonRect windowAround(HorizonIndex center, int cellsPerSide);

// Samples of to that are not in from, as at most 4 disjoint rectangles (2 for two windows of
// the same size): bands along x first (whole height of to), then along z.
[[nodiscard]] std::vector<HorizonRect> newSamples(const HorizonRect& from, const HorizonRect& to);

// Slot of a sample in a toroidal buffer of side x side slots: floorMod on each axis.
[[nodiscard]] HorizonIndex slotOf(HorizonIndex sample, int side);

// The slots of a rectangle of at most side x side samples, as at most 4 rectangles of
// [0, side) x [0, side), for an upload to a toroidal texture.
[[nodiscard]] std::vector<HorizonRect> slotRects(const HorizonRect& rect, int side);

} // namespace terrain
