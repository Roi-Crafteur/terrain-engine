// The water of the rapids of the falls of the small world as Unreal gets it (phase 12A): the core
// decides their ramp and their blocks, Unreal only draws them.
//   - Mesh: over the rapids (runs of the corridors kRampLength meters long at least whose water
//     comes down more than kRampSlope and less than kFace meters per meter), every kSampleStep
//     meters along their axis kRampEnd meters off their ends and off their blocks, the cell of
//     the tiles of LOD 0 and 1 holding the point is drawn (a corner of it wet) and never cut
//     (buildTileWater: two corners of a side apart by more than kWaterCutSlope times it): the
//     surface goes down the ramp without a step or a hole (the lips of the drops below never
//     recede into it).
//   - Levels: the level of the vertices of those cells (TileData::waterLevels, the UV0 of the
//     tiles in Unreal) is that of the water of the field there, within kLevelOff meters; on a
//     bank where the field is dry, under its ground (the level of the wet column beside it); a
//     cell off the blocks of the reach (a vertex beside one takes the levels of the wet columns of
//     its cell only).
//   - Ends: within kEndsOut meters of the top and of the foot of the rapids of the steps (their
//     valley carved from above their water), the slope of their water along the axis of the
//     course (RiverPoint::axisX, the levels of the reach) changes by kSlopeChange at most between
//     two steps kEndsStep meters long (no kink where they start and end: their foam fades in and
//     out), the faces of the drops aside (steeper than kRampMost, kFaceMargin meters around).
#include "falls_support.hpp"
#include "tile_water_mesh.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

using falls::Reaches;
using falls::World;
using terrain::RiverPoint;
using terrain::RiverReach;
using terrain::TileData;

constexpr double kRunGap = 20.0;
constexpr double kRampLength = 6.0;
constexpr double kRampSlope = 0.15;
constexpr double kFace = 1.0;
constexpr double kRampEnd = 5.0;
constexpr double kSampleStep = 2.0;
constexpr double kLevelOff = 0.1;
constexpr int kSide = terrain::kGridSamples;
constexpr double kEndsOut = 6.0;
constexpr double kEndsStep = 0.5;
constexpr double kSlopeChange = 0.2;
constexpr double kRampMost = 0.6;
constexpr double kFaceMargin = 1.0;
constexpr float kSteppedValley = 0.01f;

struct Rapids {
    std::size_t first = 0;
    std::size_t last = 0;
};

std::vector<Rapids> rapidsOf(const RiverReach& reach, const std::vector<double>& s) {
    const auto& p = reach.points;
    const auto sloped = [&](std::size_t i) {
        const double run = s[i + 1] - s[i];
        const double drop = static_cast<double>(p[i].level - p[i + 1].level);
        return run > 0.0 && run < kRunGap && drop > kRampSlope * run && !(drop > kFace * run);
    };
    std::vector<Rapids> out;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!sloped(i)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && sloped(j)) {
            ++j;
        }
        if (s[j] - s[i] >= kRampLength) {
            out.push_back({i, j});
        }
        i = j - 1;
    }
    return out;
}

using Key = std::pair<std::array<std::int32_t, 2>, int>;

struct Checks {
    std::size_t cells = 0;
    std::size_t dry = 0; // no corner wet: not drawn
    std::size_t cut = 0;
    std::size_t vertices = 0;
    std::size_t wrong = 0;
    std::map<Key, TileData> tiles;
    std::map<Key, std::set<std::pair<int, int>>> rampCells;
    std::vector<terrain::RiverBlock> blocks; // of the reaches of the rapids
};

// Whether (x, z) lies within margin meters of the water off a block (wrapped coordinates).
bool nearBlock(const Checks& c, double x, double z, double margin) {
    return std::any_of(c.blocks.begin(), c.blocks.end(), [&](const terrain::RiverBlock& b) {
        double dx = std::abs(x - terrain::wrapCoordinate(b.x, golden::kSmallPlanet.x));
        double dz = std::abs(z - terrain::wrapCoordinate(b.z, golden::kSmallPlanet.z));
        dx = std::min(dx, golden::kSmallPlanet.x - dx);
        dz = std::min(dz, golden::kSmallPlanet.z - dz);
        return std::hypot(dx, dz) < static_cast<double>(b.radius) + margin;
    });
}

// The cell of the tile of lod holding (x, z): drawn and not cut as buildTileWater draws it.
void checkCell(const terrain::TerrainTiles& tiles, double x, double z, int lod, Checks& c) {
    x = terrain::wrapCoordinate(x, golden::kSmallPlanet.x);
    z = terrain::wrapCoordinate(z, golden::kSmallPlanet.z);
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    const terrain::TileKey key = terrain::tileContaining(chunk, lod);
    const Key k{{key.origin.x, key.origin.z}, lod};
    auto it = c.tiles.find(k);
    if (it == c.tiles.end()) {
        it = c.tiles.emplace(k, tiles.generate({key, 0})).first;
    }
    const TileData& t = it->second;
    const double spacing = terrain::tileCellMeters(lod);
    const int i = std::clamp(static_cast<int>(std::floor((x - t.originX) / spacing)), 0, kSide - 2);
    const int n = std::clamp(static_cast<int>(std::floor((z - t.originZ) / spacing)), 0, kSide - 2);
    std::array<double, 4> levels{};
    std::array<bool, 4> wet{};
    double sum = 0.0;
    int wets = 0;
    for (int corner = 0; corner < 4; ++corner) {
        const auto at = static_cast<std::size_t>((n + corner / 2) * kSide + i + corner % 2);
        const terrain::WaterColumn& w = t.waterColumns[at];
        wet[static_cast<std::size_t>(corner)] = w.kind != terrain::WaterKind::None;
        levels[static_cast<std::size_t>(corner)] = w.level;
        sum += wet[static_cast<std::size_t>(corner)] ? static_cast<double>(w.level) : 0.0;
        wets += wet[static_cast<std::size_t>(corner)] ? 1 : 0;
    }
    ++c.cells;
    if (wets == 0) {
        ++c.dry;
        return;
    }
    for (int corner = 0; corner < 4; ++corner) {
        if (!wet[static_cast<std::size_t>(corner)]) {
            levels[static_cast<std::size_t>(corner)] = sum / wets;
        }
    }
    const double limit = terrain::unreal::kWaterCutSlope * spacing;
    c.cut += std::abs(levels[0] - levels[1]) > limit || std::abs(levels[2] - levels[3]) > limit ||
                     std::abs(levels[0] - levels[2]) > limit ||
                     std::abs(levels[1] - levels[3]) > limit
                 ? 1
                 : 0;
    c.rampCells[k].insert({i, n});
}

// The levels of the vertices of the cells of the rapids against the field.
void checkLevels(const World& w, Checks& c) {
    for (const auto& [key, cells] : c.rampCells) {
        const TileData& t = c.tiles.at(key);
        const double spacing = terrain::tileCellMeters(key.second);
        for (std::size_t v = 0; v < t.mesh.vertices.size(); ++v) {
            const double x = t.originX + t.mesh.vertices[v].x;
            const double z = t.originZ + t.mesh.vertices[v].z;
            const auto cell =
                std::make_pair(static_cast<int>(std::floor((x - t.originX) / spacing)),
                               static_cast<int>(std::floor((z - t.originZ) / spacing)));
            // Beside a block, a vertex takes the level of the wet columns of its cell only.
            if (cells.count(cell) == 0 || !(t.waterLevels[v] > terrain::kNoWaterLevel) ||
                nearBlock(c, x, z, spacing)) {
                continue;
            }
            const terrain::HeightSample h = w.field.sampleAt(x, z);
            const double level = static_cast<double>(t.waterLevels[v]);
            ++c.vertices;
            // A bank between a wet and a dry column: the level of the wet one, under the ground.
            c.wrong +=
                h.water == terrain::WaterKind::None
                    ? (level > static_cast<double>(h.height) + kLevelOff ? 1 : 0)
                    : (std::abs(static_cast<double>(h.waterLevel) - level) > kLevelOff ? 1 : 0);
        }
    }
}

// Meters along the axis of the points of a reach (RiverPoint::axisX: their levels lie along it).
std::vector<double> axisAlong(const RiverReach& reach) {
    const auto& p = reach.points;
    std::vector<double> a(p.size(), 0.0);
    for (std::size_t i = 1; i < p.size(); ++i) {
        a[i] = a[i - 1] + std::hypot(p[i].axisX - p[i - 1].axisX, p[i].axisZ - p[i - 1].axisZ);
    }
    return a;
}

// The most the slope of the water of reach changes between two steps kEndsStep long from a0 to a1
// along its axis (a: meters along it), off the faces of the drops: its segments steeper than
// kRampMost, kFaceMargin meters around them.
double slopeChange(const RiverReach& reach, const std::vector<double>& a, double a0, double a1) {
    const auto& p = reach.points;
    const auto face = [&](std::size_t i) {
        const double run = a[i + 1] - a[i];
        return run > 0.0 && static_cast<double>(p[i].level - p[i + 1].level) > kRampMost * run;
    };
    const auto levelAt = [&](double at, bool& onFace) {
        std::size_t i = 0;
        while (i + 2 < p.size() && a[i + 1] < at) {
            ++i;
        }
        for (std::size_t k = 0; k + 1 < p.size(); ++k) {
            onFace = onFace || (face(k) && a[k + 1] > at - kFaceMargin && a[k] < at + kFaceMargin);
        }
        const double run = a[i + 1] - a[i];
        const double t = run > 0.0 ? std::clamp((at - a[i]) / run, 0.0, 1.0) : 1.0;
        return static_cast<double>(p[i].level) +
               t * static_cast<double>(p[i + 1].level - p[i].level);
    };
    double most = 0.0;
    for (double at = std::max(a0, kEndsStep); at + kEndsStep <= std::min(a1, a.back());
         at += kEndsStep) {
        bool onFace = false;
        const double before = levelAt(at - kEndsStep, onFace);
        const double here = levelAt(at, onFace);
        const double after = levelAt(at + kEndsStep, onFace);
        if (!onFace) {
            most = std::max(most, std::abs((before - here) - (here - after)) / kEndsStep);
        }
    }
    return most;
}

// Whether the run of reach from point first to point last is rapids of a stretch in steps (its
// valley carved from above its water, RiverPoint::valley), not the slope of a river.
bool stepped(const RiverReach& reach, std::size_t first, std::size_t last) {
    for (std::size_t i = first; i <= last; ++i) {
        if (std::abs(reach.points[i].valley - reach.points[i].level) > kSteppedValley) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    const World w = falls::makeWorld();
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet,
                                      w.hydrology);
    Reaches reaches(*w.hydrology);
    Checks c;
    std::size_t rapids = 0;
    std::size_t ends = 0;
    double change = 0.0;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        std::vector<double> s(p.size(), 0.0);
        for (std::size_t i = 1; i < p.size(); ++i) {
            s[i] = s[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
        }
        const std::vector<double> axis = axisAlong(reach);
        c.blocks.insert(c.blocks.end(), reach.blocks.begin(), reach.blocks.end());
        for (const Rapids& run : rapidsOf(reach, s)) {
            ++rapids;
            if (stepped(reach, run.first, run.last)) {
                ++ends;
                for (const double end : {axis[run.first], axis[run.last]}) {
                    change = std::max(change,
                                      slopeChange(reach, axis, end - kEndsOut, end + kEndsOut));
                }
            }
            for (double along = s[run.first] + kRampEnd; along <= s[run.last] - kRampEnd;
                 along += kSampleStep) {
                std::size_t i = run.first;
                while (i + 1 < run.last && s[i + 1] < along) {
                    ++i;
                }
                const double t = std::clamp((along - s[i]) / (s[i + 1] - s[i]), 0.0, 1.0);
                const double x = p[i].x + t * (p[i + 1].x - p[i].x);
                const double z = p[i].z + t * (p[i + 1].z - p[i].z);
                const bool block = std::any_of(
                    reach.blocks.begin(), reach.blocks.end(), [&](const terrain::RiverBlock& b) {
                        return std::hypot(b.x - x, b.z - z) < static_cast<double>(b.radius) + 1.0;
                    });
                for (const int lod : {0, 1}) {
                    if (!block) {
                        checkCell(tiles, x, z, lod, c);
                    }
                }
            }
        }
    }
    checkLevels(w, c);
    std::printf("%zu rapids of the falls: %zu cells of the tiles of LOD 0 and 1 over them, %zu "
                "without water, %zu cut; %zu vertices of them under water, %zu off the level of "
                "the field by more than %.1f m\n",
                rapids, c.cells, c.dry, c.cut, c.vertices, c.wrong, kLevelOff);
    std::printf("%zu of them in steps: within %.0f m of their ends, the slope of their water "
                "changes by %.3f at most every %.1f m\n",
                ends, kEndsOut, change, kEndsStep);
    TEST_CHECK(rapids >= 10 && ends >= 10 && c.cells >= 500 && c.vertices >= 1000);
    TEST_CHECK(change <= kSlopeChange);
    TEST_CHECK(c.dry == 0 && c.cut == 0);
    TEST_CHECK(c.wrong == 0);
    return test::result();
}
