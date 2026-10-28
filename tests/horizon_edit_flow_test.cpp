// The player edits reach the far horizon (horizon_edits.hpp, HorizonStreamer): a mountain razed
// by a box of 1 km x 1 km x 500 m and a heap of earth seen from kilometers away, their profile
// following the edited ground; a crater of 10 m on level 0 and almost nothing far away. An edit
// staged while an epoch is computed waits for the next one, which computes again only the
// samples around it; 0 and 3 worker threads give the same bits, those of a fresh horizon, pinned
// by a golden checksum; the samples far from the edits keep their bits.
#include "terrain/io/horizon_panorama.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/horizon_streamer.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "horizon_golden_support.hpp"
#include "horizon_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::HorizonEditMap;
using terrain::HorizonStreamer;
using terrain::TerrainTiles;
using terrain::VolumeEdit;

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// The places of the edits: a mountain razed whole, a crater and a heap on lower ground.
struct Places {
    int x0 = 0; // the low corner of the razed square of 1 km
    int z0 = 0;
    int floor = 0; // its lowest block
    BlockCoord crater;
    BlockCoord heap;
};

Places placesOf(const TerrainTiles& tiles) {
    Places places;
    const ChunkCoord mountain = streaming_test::mountainChunk(tiles.heightfield());
    places.x0 = mountain.x * 16 + 5;
    places.z0 = mountain.z * 16 + 7;
    int top = groundAt(tiles, places.x0, places.z0);
    for (int dz = 0; dz <= 999; dz += 25) {
        for (int dx = 0; dx <= 999; dx += 25) {
            top = std::max(top, groundAt(tiles, places.x0 + dx, places.z0 + dz));
        }
    }
    places.floor = top - 499;
    // On an even sample of level 0.
    const int cx = (places.x0 + 1800) & ~15;
    const int cz = (places.z0 + 600) & ~15;
    places.crater = {cx, groundAt(tiles, cx, cz), cz};
    const int hx = places.x0 - 1500;
    const int hz = places.z0 + 2600;
    places.heap = {hx, groundAt(tiles, hx, hz) - 10, hz};
    return places;
}

VolumeEdit razedEdit(const Places& p) {
    return {EditShape::Box, EditMode::Remove, {p.x0, p.floor, p.z0}, {1000, 500, 1000}, 0};
}

VolumeEdit craterEdit(const Places& p) {
    return {EditShape::Sphere, EditMode::Remove, p.crater, {}, 5 * 16};
}

constexpr int kHeapRadius = 100;

VolumeEdit heapEdit(const Places& p) {
    return {EditShape::Sphere, EditMode::Add, p.heap, {}, kHeapRadius * 16};
}

// The observer: 3 km south of the razed square, 2 m over the ground.
terrain::PanoramaParams eyeOf(const TerrainTiles& tiles, const Places& p) {
    terrain::PanoramaParams eye;
    eye.eyeX = p.x0 + 500.0;
    eye.eyeZ = p.z0 + 4000.0;
    eye.eyeHeight = tiles.heightfield().heightAt(eye.eyeX, eye.eyeZ) + 2.0;
    eye.width = 7200; // 0.05 degree per column
    return eye;
}

// The column of the panorama facing (x, z), counted from the north (negative west of it), and
// the elevation of a tangent, degrees.
int columnFacing(const terrain::PanoramaParams& eye, double x, double z) {
    const double turns = std::atan2(x - eye.eyeX, -(z - eye.eyeZ)) / (2.0 * std::numbers::pi);
    return static_cast<int>(std::floor(turns * eye.width));
}

double degrees(double tangent) {
    return std::atan(tangent) * 180.0 / std::numbers::pi;
}

// Mean absolute difference of two silhouettes, degrees.
double meanDifference(const terrain::Silhouette& a, const terrain::Silhouette& b) {
    double sum = 0.0;
    for (std::size_t c = 0; c < a.tangent.size(); ++c) {
        sum += std::abs(degrees(a.tangent[c]) - degrees(b.tangent[c]));
    }
    return sum / static_cast<double>(a.tangent.size());
}

// The profile of a square of the world seen from the eye: the silhouette of the columns facing
// its middle, over the distances it spans, a margin inside it: the rays stay in the square.
struct Profile {
    int first = 0;
    int columns = 0;
    double near = 0.0;
    double far = 0.0;

    [[nodiscard]] terrain::Silhouette of(const terrain::GroundHeight& ground,
                                         terrain::PanoramaParams eye) const {
        eye.minDistance = near;
        return terrain::traceSilhouette(ground, eye, first, columns, far, 1024.0);
    }
};

Profile profileOf(const terrain::PanoramaParams& eye, double x0, double z0, double side,
                  double margin) {
    const double x1 = x0 + side - margin;
    const double z1 = z0 + side - margin;
    x0 += margin;
    z0 += margin;
    int first = columnFacing(eye, x0, z0);
    int last = first;
    double near = std::hypot(x0 - eye.eyeX, z0 - eye.eyeZ);
    double far = near;
    for (const double x : {x0, x1}) {
        for (const double z : {z0, z1}) {
            first = std::min(first, columnFacing(eye, x, z));
            last = std::max(last, columnFacing(eye, x, z));
            near = std::min(near, std::hypot(x - eye.eyeX, z - eye.eyeZ));
            far = std::max(far, std::hypot(x - eye.eyeX, z - eye.eyeZ));
        }
    }
    // The middle half of the columns, whose rays cross the square from near to far.
    const int quarter = (last - first) / 4;
    return {first + quarter, last - first + 1 - 2 * quarter, near + margin, far - margin};
}

// The relief with the razed square at the floor the tiles show and the heap as a sphere cap.
terrain::GroundHeight truthOf(const TerrainTiles& tiles, const Places& p, float razedFloor) {
    return [&tiles, p, razedFloor](double x, double z) -> std::optional<float> {
        float height = tiles.heightfield().heightAt(x, z);
        if (x >= p.x0 - 0.5 && x <= p.x0 + 999.5 && z >= p.z0 - 0.5 && z <= p.z0 + 999.5) {
            height = std::min(height, razedFloor);
        }
        const double d2 = (x - p.heap.x) * (x - p.heap.x) + (z - p.heap.z) * (z - p.heap.z);
        constexpr double r2 = kHeapRadius * kHeapRadius;
        if (d2 < r2) {
            height = std::max(height, static_cast<float>(p.heap.y + std::sqrt(r2 - d2)));
        }
        return height;
    };
}

terrain::HorizonParams paramsOf(int cellsPerSide) {
    terrain::HorizonParams params = horizon_test::smallParams();
    params.cellsPerSide = cellsPerSide;
    return params;
}

struct Session {
    TerrainTiles tiles{streaming_test::kSeed};
    HorizonEditMap map{tiles.heightfield()};
    terrain::HorizonSampler sampler;
    HorizonStreamer streamer;

    explicit Session(int threads, int cellsPerSide = 64)
        : sampler(tiles.heightfield(), tiles.ecology(), paramsOf(cellsPerSide), map.source()),
          streamer(sampler, {threads, 1024}, &map) {}

    void edit(const VolumeEdit& edit) { map.stage(tiles, tiles.applyEdit(edit)); }
};

// The samples of a level, from the low corner of its window.
std::vector<terrain::HorizonCell> windowOf(const terrain::HorizonLevel& level) {
    std::vector<terrain::HorizonCell> cells;
    for (int k = 0; k < level.side(); ++k) {
        for (int i = 0; i < level.side(); ++i) {
            cells.push_back(level.atWindow(i, k));
        }
    }
    return cells;
}

// Samples written by a commit.
std::int64_t written(const terrain::HorizonChanges& changes) {
    std::int64_t samples = 0;
    for (const terrain::HorizonLevelChange& change : changes.levels) {
        for (const terrain::HorizonRect& rect : change.written) {
            samples += rect.count();
        }
    }
    return samples;
}

// The far silhouettes of the razed mountain and of the heap follow the edits.
void testSilhouettes() {
    Session session(3, 128);
    const Places p = placesOf(session.tiles);
    const terrain::PanoramaParams eye = eyeOf(session.tiles, p);
    horizon_test::settle(session.streamer, eye.eyeX, eye.eyeZ);
    const terrain::GroundHeight levels = terrain::clipmapGround(session.streamer.clipmap());
    const Profile mountain = profileOf(eye, p.x0, p.z0, 1000.0, 100.0);
    const Profile heap =
        profileOf(eye, p.heap.x - kHeapRadius, p.heap.z - kHeapRadius, 2.0 * kHeapRadius, 20.0);
    const terrain::Silhouette mountainBefore = mountain.of(levels, eye);
    const terrain::Silhouette heapBefore = heap.of(levels, eye);
    session.edit(razedEdit(p));
    session.edit(heapEdit(p));
    session.streamer.update(eye.eyeX, eye.eyeZ);
    TEST_CHECK(session.streamer.busy() && !session.map.hasStaged());
    horizon_test::settle(session.streamer, eye.eyeX, eye.eyeZ);
    const std::optional<float> floor = session.map.columnGround(
        {(p.x0 + 500) / terrain::kEditColumnSpacing, (p.z0 + 500) / terrain::kEditColumnSpacing});
    TEST_CHECK(floor.has_value());
    const terrain::GroundHeight truth = truthOf(session.tiles, p, floor.value_or(0.0f));
    const terrain::Silhouette mountainAfter = mountain.of(levels, eye);
    const terrain::Silhouette mountainTruth = mountain.of(truth, eye);
    const terrain::Silhouette heapAfter = heap.of(levels, eye);
    const terrain::Silhouette heapTruth = heap.of(truth, eye);
    const double mountainWas = meanDifference(mountainBefore, mountainTruth);
    const double mountainIs = meanDifference(mountainAfter, mountainTruth);
    const double heapWas = meanDifference(heapBefore, heapTruth);
    const double heapIs = meanDifference(heapAfter, heapTruth);
    std::printf("razed mountain seen from 3.2 km (%d columns): %.3f degrees from the edited "
                "profile before the edit, %.3f after\n",
                mountain.columns, mountainWas, mountainIs);
    std::printf("heap of %d m seen from %.1f km (%d columns): %.3f degrees before, %.3f after\n",
                kHeapRadius, std::hypot(eye.eyeX - p.heap.x, eye.eyeZ - p.heap.z) / 1000.0,
                heap.columns, heapWas, heapIs);
    // Inside the razed square, every sample of the levels is its floor.
    TEST_CHECK(mountainWas > 3.0 && mountainIs < 0.05);
    TEST_CHECK(heapWas > 0.2 && heapIs < 0.25 * heapWas);
}

// A crater of 10 m: on level 0 as on the tiles, a few centimeters at most far away.
void testCrater() {
    Session session(0);
    const Places p = placesOf(session.tiles);
    const double x = p.crater.x;
    const double z = p.crater.z;
    horizon_test::settle(session.streamer, x, z);
    std::vector<terrain::HorizonCell> before;
    std::vector<std::vector<terrain::HorizonCell>> windows;
    for (int n = 0; n < session.streamer.clipmap().levelCount(); ++n) {
        const terrain::HorizonLevel& level = session.streamer.level(n);
        before.push_back(*level.find(terrain::canonicalCenter(session.sampler.params(), n, x, z)));
        windows.push_back(windowOf(level));
    }
    session.edit(craterEdit(p));
    session.streamer.update(x, z);
    session.streamer.waitIdle();
    const terrain::HorizonChanges changes = session.streamer.update(x, z);
    std::printf("crater of 10 m: %zu edited columns, %lld samples computed again\n",
                session.map.columnCount(), static_cast<long long>(written(changes)));
    TEST_CHECK(session.map.columnCount() > 0 && written(changes) < 400);
    const terrain::HorizonCell* center = session.streamer.level(0).find(
        terrain::canonicalCenter(session.streamer.clipmap().params(), 0, x, z));
    const std::optional<float> column = session.map.columnGround(
        {p.crater.x / terrain::kEditColumnSpacing, p.crater.z / terrain::kEditColumnSpacing});
    TEST_CHECK(center && column && center->height == *column && center->height < before[0].height);
    TEST_CHECK(center && (center->flags & terrain::kHorizonEdited) != 0);
    for (int n = 5; n < session.streamer.clipmap().levelCount(); ++n) {
        const terrain::HorizonLevel& level = session.streamer.level(n);
        const terrain::HorizonCell* far =
            level.find(terrain::canonicalCenter(session.sampler.params(), n, x, z));
        TEST_CHECK(far &&
                   std::abs(far->height - before[static_cast<std::size_t>(n)].height) < 0.05f);
    }
    // Only the samples next to the crater changed: within a pitch and a half of it.
    std::int64_t changed = 0;
    bool near = true;
    for (int n = 0; n < session.streamer.clipmap().levelCount(); ++n) {
        const terrain::HorizonLevel& level = session.streamer.level(n);
        const std::vector<terrain::HorizonCell> after = windowOf(level);
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                const std::size_t at = static_cast<std::size_t>(k * level.side() + i);
                if (after[at] == windows[static_cast<std::size_t>(n)][at]) {
                    continue;
                }
                ++changed;
                const double sx = level.originX(x) + i * level.pitch();
                const double sz = level.originZ(z) + k * level.pitch();
                near = near && std::abs(sx - x) <= 1.5 * level.pitch() + 8.0 &&
                       std::abs(sz - z) <= 1.5 * level.pitch() + 8.0;
            }
        }
    }
    std::printf("crater of 10 m: %lld samples changed, all next to it: %s\n",
                static_cast<long long>(changed), near ? "yes" : "no");
    TEST_CHECK(changed > 0 && near);
}

// The content of every level, summed as the goldens of the horizon.
std::uint64_t contentChecksum(const terrain::HorizonClipmap& clipmap) {
    golden::Checksum sum;
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                golden::horizon_detail::addCell(sum, level.atWindow(i, k));
            }
        }
    }
    return sum.value();
}

// Edits staged while an epoch is computed wait for the next; 0 and 3 threads, the same bits.
void testDeterminism() {
    std::vector<std::uint64_t> sums;
    for (const int threads : {0, 3}) {
        Session session(threads);
        const Places p = placesOf(session.tiles);
        const double x = p.x0 - 600.0;
        const double z = p.z0 + 500.0;
        horizon_test::settle(session.streamer, x, z);
        session.edit(razedEdit(p));
        session.streamer.update(x + 400.0, z); // the levels move: an epoch in flight
        TEST_CHECK(session.streamer.busy() && !session.map.hasStaged());
        session.edit(craterEdit(p));
        session.edit(heapEdit(p));
        session.streamer.update(x + 400.0, z);
        TEST_CHECK(threads != 0 || session.map.hasStaged()); // the epoch is not computed yet
        horizon_test::settle(session.streamer, x + 400.0, z);
        TEST_CHECK(!session.map.hasStaged());
        TEST_CHECK(session.streamer.clipmap().pendingInvalidations() == 0);
        TEST_CHECK(
            horizon_test::sameAsFresh(session.streamer.clipmap(), session.sampler, x + 400.0, z));
        sums.push_back(contentChecksum(session.streamer.clipmap()));
    }
    TEST_CHECK(sums[0] == sums[1]);
    golden::report("the horizon with a razed mountain, a crater and a heap",
                   {{"edited horizon", sums[0], 0x75963890668CF506ULL}});
}

} // namespace

int main() {
    testSilhouettes();
    testCrater();
    testDeterminism();
    return test::result();
}
