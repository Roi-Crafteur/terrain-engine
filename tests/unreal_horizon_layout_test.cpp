// Layout of the far horizon of the Unreal plugin: the squares the bodies of the levels leave out,
// the window of each finer level in them (its 9 positions, along a path across the edges of the
// world), the hole under the tiles against the tiles selectTiles chooses, every point of the
// coarsest window drawn by exactly one level outside that hole, the origins of the levels at
// their images nearest to the player (continuous across an edge, whole worlds for a world turn),
// the curvature of the planet, and the crest ratio of the editor setting.
#include "horizon_layout.hpp"

#include "test_assert.hpp"

#include "terrain/chunk_coord.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonLevel;
using terrain::HorizonParams;
using terrain::LodParams;
using terrain::unreal::CellRect;
using terrain::unreal::CorePoint;
using terrain::unreal::TileHole;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;

// Windows of 128 cells, with tiles that end inside level 0 (its radius is 504 m).
HorizonParams mediumParams() {
    HorizonParams params;
    params.cellsPerSide = 128;
    return params;
}

LodParams mediumLod() {
    return {{1, 3, 7}, 23};
}

// The windows of every level, moved along the path of an observer as a clipmap moves them.
class Windows {
public:
    explicit Windows(const HorizonParams& params) : params_(params) {
        for (int n = 0; n < params.levels; ++n) {
            levels_.emplace_back(params, n);
        }
    }

    void moveTo(double x, double z) {
        for (HorizonLevel& level : levels_) {
            const terrain::HorizonIndex target =
                terrain::canonicalCenter(params_, level.level(), x, z);
            level.moveTo(level.placed()
                             ? terrain::moveCenter(level.center(), target, level.counts())
                             : target);
        }
    }

    [[nodiscard]] const HorizonLevel& level(int n) const {
        return levels_[static_cast<std::size_t>(n)];
    }
    [[nodiscard]] const HorizonParams& params() const { return params_; }

private:
    HorizonParams params_;
    std::vector<HorizonLevel> levels_;
};

void testBodyHoles() {
    const HorizonParams params;
    TEST_CHECK(terrain::unreal::bodyHole(params, 1, 47) == CellRect{63, 63, 193, 193});
    TEST_CHECK(terrain::unreal::bodyHole(params, 9, 47) == CellRect{63, 63, 193, 193});
    TEST_CHECK(terrain::unreal::bodyHole(params, 0, 47) == CellRect{81, 81, 175, 175});
    TEST_CHECK(terrain::unreal::centeredSquare(64, 17) == CellRect{15, 15, 49, 49});
    TEST_CHECK(CellRect{2, 3, 5, 7}.contains(4, 6) && !CellRect{2, 3, 5, 7}.contains(5, 6));
    TEST_CHECK(CellRect{0, 0, 9, 9}.contains(CellRect{0, 1, 9, 8}));
    TEST_CHECK(!CellRect{0, 0, 9, 9}.contains(CellRect{0, 1, 10, 8}));
}

void testTileHole() {
    const HorizonParams params;
    TEST_CHECK(terrain::unreal::tileCoverage(LodParams{}) == 512.0);
    TEST_CHECK(terrain::unreal::fineTilesReach(LodParams{}) == 368.0);
    // The fine tiles end 376 m from the center of level 0, the tiles cover 504 m: 128 m of band.
    TEST_CHECK((terrain::unreal::tileHole(params, LodParams{}) == TileHole{47, 128.0}));
    // A view of 45 chunks (tiles up to about 850 m): the band stays 128 m wide.
    TEST_CHECK((terrain::unreal::tileHole(params, {{4, 8, 16}, 45}) == TileHole{73, 128.0}));
    // A shorter view keeps the fine tiles in the hole, down to a band of 64 m.
    TEST_CHECK((terrain::unreal::tileHole(params, {{4, 8, 16}, 28}) == TileHole{47, 64.0}));
    bool narrow = false;
    try {
        static_cast<void>(terrain::unreal::tileHole(params, {{4, 8, 16}, 27}));
    } catch (const std::invalid_argument&) {
        narrow = true;
    }
    TEST_CHECK(narrow);
    // Tiles beyond level 0 (more than 55 chunks): refused by the core.
    bool far = false;
    try {
        static_cast<void>(terrain::unreal::tileHole(params, {{4, 8, 16}, 56}));
    } catch (const std::invalid_argument&) {
        far = true;
    }
    TEST_CHECK(far);
    static_cast<void>(terrain::unreal::tileHole(params, {{4, 8, 16}, 55}));
    TEST_CHECK((terrain::unreal::tileHole(mediumParams(), mediumLod()) == TileHole{29, 128.0}));
}

std::int64_t chunkKey(terrain::ChunkCoord chunk) {
    return (std::int64_t{chunk.x} << 32) ^ static_cast<std::uint32_t>(chunk.z);
}

// Farthest a tile reaches along an axis from any position of the player in its chunk.
double farthest(std::int32_t first, int chunks, std::int32_t player) {
    const double low = first * terrain::kChunkSizeMeters;
    const double high = (first + chunks) * terrain::kChunkSizeMeters;
    const double p0 = player * terrain::kChunkSizeMeters;
    const double p1 = p0 + terrain::kChunkSizeMeters;
    return std::max({std::abs(low - p0), std::abs(low - p1), std::abs(high - p0),
                     std::abs(high - p1)});
}

// The tiles selectTiles chooses: every chunk within the coverage is in one of them, the tiles
// finer than LOD 3 end within fineTilesReach, and some reach it within one chunk.
void testTilesAgainstSelection(const LodParams& lod) {
    const double reach = terrain::unreal::fineTilesReach(lod);
    double reached = 0.0;
    for (std::int32_t pz : {-9, -1, 0, 3, 7, 1000}) {
        for (std::int32_t px = -9; px <= 9; ++px) {
            const terrain::ChunkCoord player{px, pz};
            std::unordered_set<std::int64_t> covered;
            for (const terrain::TileKey& tile : terrain::selectTiles(player, lod)) {
                const int chunks = terrain::tileChunks(tile.lod);
                for (int k = 0; k < chunks; ++k) {
                    for (int i = 0; i < chunks; ++i) {
                        covered.insert(chunkKey({tile.origin.x + i, tile.origin.z + k}));
                    }
                }
                if (tile.lod < terrain::kMaxLod) {
                    const double far = std::max(farthest(tile.origin.x, chunks, px),
                                                farthest(tile.origin.z, chunks, pz));
                    TEST_CHECK(far <= reach);
                    reached = std::max(reached, far);
                }
            }
            const int view = lod.viewDistance;
            bool all = true;
            for (int k = -view; k <= view; ++k) {
                for (int i = -view; i <= view; ++i) {
                    all = all && covered.contains(chunkKey({px + i, pz + k}));
                }
            }
            TEST_CHECK(all);
        }
    }
    TEST_CHECK(reached > reach - terrain::kChunkSizeMeters);
}

// Levels drawing (x, z) near the observer (ox, oz): their bodies and trims; -1 inside the hole
// under the tiles.
int drawnBy(const Windows& windows, const TileHole& hole, double ox, double oz, double x,
            double z) {
    const HorizonParams& params = windows.params();
    const int cells = params.cellsPerSide;
    int count = 0;
    for (int n = 0; n < params.levels; ++n) {
        const HorizonLevel& level = windows.level(n);
        const double u = (x - level.originX(ox)) / level.pitch();
        const double v = (z - level.originZ(oz)) / level.pitch();
        if (u < 0.0 || v < 0.0 || u >= cells || v >= cells) {
            continue;
        }
        const int i = static_cast<int>(u);
        const int k = static_cast<int>(v);
        if (!terrain::unreal::bodyHole(params, n, hole.halfCells).contains(i, k)) {
            ++count;
        } else if (n == 0) {
            return -1;
        } else if (!terrain::unreal::finerWindowCells(level, windows.level(n - 1)).contains(i, k)) {
            ++count;
        }
    }
    return count;
}

// Points across the window of every level, never on a sample line (all are multiples of 8 m):
// each is drawn by exactly one level, or lies in the tile hole.
void checkCoverage(const Windows& windows, const TileHole& hole, double ox, double oz) {
    const HorizonParams& params = windows.params();
    constexpr int kPoints = 41;
    int inHole = 0;
    int wrong = 0;
    for (int n = 0; n < params.levels; ++n) {
        const HorizonLevel& level = windows.level(n);
        const double step = params.cellsPerSide * level.pitch() / (kPoints + 0.0137);
        for (int b = 0; b < kPoints; ++b) {
            for (int a = 0; a < kPoints; ++a) {
                const double x = level.originX(ox) + (a + 0.31) * step + 0.123;
                const double z = level.originZ(oz) + (b + 0.47) * step + 0.217;
                const int count = drawnBy(windows, hole, ox, oz, x, z);
                inHole += count == -1 ? 1 : 0;
                wrong += count == -1 || count == 1 ? 0 : 1;
            }
        }
    }
    TEST_CHECK(wrong == 0);
    TEST_CHECK(inHole > 0);
}

// Along a path across the north-east corner of the world: the finer windows stay in the squares
// of the bodies and take their 9 positions, the levels cover their windows once, and the origins
// move continuously (by the step and two pitches at most, never by a world).
void testPath(const HorizonParams& params, const LodParams& lod) {
    const TileHole hole = terrain::unreal::tileHole(params, lod);
    Windows windows(params);
    const UnrealFrame frame{{11'267.75, 1401.5, -61'451.5}};
    std::set<std::pair<int, int>> positions;
    std::vector<UnrealPoint> previous;
    const int quarter = params.cellsPerSide / 4;
    for (int step = 0; step <= 400; ++step) {
        const double x = kWorld.x / 2.0 - 9'000.0 + 37.0 * step;
        const double z = -kWorld.z / 2.0 + 7'000.0 - 29.0 * step;
        windows.moveTo(x, z);
        std::vector<UnrealPoint> origins;
        for (int n = 0; n < params.levels; ++n) {
            origins.push_back(terrain::unreal::levelOrigin(windows.level(n), frame, {x, 0.0, z}));
            if (!previous.empty()) {
                // The step of the observer, and the snapping of the window to even samples.
                const double limit = (37.0 + 2.0 * windows.level(n).pitch()) * 100.0;
                const UnrealPoint& before = previous[static_cast<std::size_t>(n)];
                TEST_CHECK(std::abs(origins.back().x - before.x) <= limit);
                TEST_CHECK(std::abs(origins.back().y - before.y) <= limit);
            }
            if (n > 0) {
                const CellRect finer =
                    terrain::unreal::finerWindowCells(windows.level(n), windows.level(n - 1));
                TEST_CHECK(finer.i1 - finer.i0 == params.cellsPerSide / 2);
                TEST_CHECK(finer.k1 - finer.k0 == params.cellsPerSide / 2);
                positions.insert({finer.i0 - quarter, finer.k0 - quarter});
            }
        }
        previous = std::move(origins);
        if (step % 100 == 0) {
            checkCoverage(windows, hole, x, z);
        }
    }
    TEST_CHECK(positions.size() == 9);
    TEST_CHECK(positions.begin()->first == -1 && positions.rbegin()->first == 1);
}

// A world turn moves the observer by whole worlds: the origins move by the same centimeters.
void testWorldTurn() {
    const HorizonParams params = mediumParams();
    Windows windows(params);
    const double x = 1'048'580.0;
    const double z = -3'932'156.0;
    windows.moveTo(x, z);
    const UnrealFrame frame{{11'267.75, 1401.5, -61'451.5}};
    for (int n = 0; n < params.levels; ++n) {
        const UnrealPoint here = terrain::unreal::levelOrigin(windows.level(n), frame, {x, 0.0, z});
        const UnrealPoint there = terrain::unreal::levelOrigin(
            windows.level(n), frame, {x + 2.0 * kWorld.x, 0.0, z - kWorld.z});
        TEST_CHECK(there.x == here.x + 2.0 * kWorld.x * 100.0);
        TEST_CHECK(there.y == here.y - kWorld.z * 100.0);
        TEST_CHECK(there.z == here.z && here.z == -1401.5 * 100.0);
        // The window is centered on the observer, within one pitch.
        const double half = params.cellsPerSide / 2.0 * windows.level(n).pitch() * 100.0;
        const UnrealPoint observer = terrain::unreal::toUnreal(frame, {x, 0.0, z});
        TEST_CHECK(std::abs(here.x + half - observer.x) <= windows.level(n).pitch() * 100.0);
        TEST_CHECK(std::abs(here.y + half - observer.y) <= windows.level(n).pitch() * 100.0);
    }
}

void testCurvature() {
    const double radius = terrain::unreal::planetRadiusMeters(kWorld);
    TEST_CHECK(std::abs(radius * 2.0 * 3.14159265358979 - kWorld.x) < 1.0);
    TEST_CHECK(radius > 9'554'000.0 && radius < 9'555'000.0);
    TEST_CHECK(terrain::unreal::curvatureDropMeters(0.0, radius) == 0.0);
    const double at50 = terrain::unreal::curvatureDropMeters(50'000.0, radius);
    const double at400 = terrain::unreal::curvatureDropMeters(400'000.0, radius);
    TEST_CHECK(at50 > 130.0 && at50 < 132.0);
    TEST_CHECK(at400 > 8'370.0 && at400 < 8'380.0);
    TEST_CHECK(std::abs(at400 / at50 - 64.0) < 1e-9);
    const HorizonParams params;
    const double reach = terrain::unreal::levelReachMeters(params, 9);
    TEST_CHECK(std::abs(reach - 129.0 * 4096.0 * std::sqrt(2.0)) < 1e-6);
    TEST_CHECK(terrain::unreal::levelReachMeters(params, 0) == reach / 512.0);
}

} // namespace

// The crest ratio of the editor setting: 0 below 2, else 2, 4 or 8, the largest not above it;
// the default setting is the default of the core.
void testCrestSetting() {
    for (const auto& [setting, ratio] : {std::pair{-3, 0}, std::pair{0, 0}, std::pair{1, 0},
                                         std::pair{2, 2}, std::pair{3, 2}, std::pair{4, 4},
                                         std::pair{7, 4}, std::pair{8, 8}, std::pair{40, 8}}) {
        TEST_CHECK(terrain::unreal::crestRatioSetting(setting) == ratio);
    }
    TEST_CHECK(terrain::unreal::crestRatioSetting(2) == HorizonParams{}.crestRatio);
}

int main() {
    testBodyHoles();
    testTileHole();
    testTilesAgainstSelection(LodParams{});
    testTilesAgainstSelection({{4, 8, 16}, 45});
    testTilesAgainstSelection(mediumLod());
    testPath(mediumParams(), mediumLod());
    testPath(HorizonParams{}, LodParams{});
    testWorldTurn();
    testCurvature();
    testCrestSetting();
    return test::result();
}
