// The falls in steps of the small world (phase 11B): the slot of a fall keeps its rim at the foot
// of each step (no tooth where the water drops), and outside the corridors of the falls (no reach
// with a step of its water steeper than kSteep within kCorridor meters) every bit of the samples
// and of the tiles stays as before them (a golden checksum).
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "falls_support.hpp"
#include "golden_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace {

using falls::depthAt;
using falls::endsOf;
using falls::Jumps;
using falls::jumpsAround;
using falls::kBedJump;
using falls::kEndStep;
using falls::kNeighbors;
using falls::kWaterJump;
using falls::RunEnd;
using falls::kCorridor;
using falls::kFloorRise;
using falls::kSteep;
using falls::makeWorld;
using falls::quantile;
using falls::rimsAt;
using falls::steep;
using falls::steepReaches;
using falls::Reaches;
using falls::World;
using terrain::RiverReach;

// Places on the rivers (points of the graph, one segment in 9) outside the corridors of the falls.
std::vector<terrain::RiverVertex> placesOutside(const World& w, Reaches& reaches,
                                                std::size_t most) {
    std::vector<terrain::RiverVertex> out;
    const auto& segments = w.planet->rivers().segments;
    for (std::size_t s = 0; s < segments.size() && out.size() < most; s += 9) {
        const terrain::RiverVertex& v = segments[s].vertices[segments[s].vertices.size() / 2];
        bool outside = true;
        for (const std::size_t r : w.hydrology->reachesNear(v.x, v.z, kCorridor)) {
            outside = outside && !steep(reaches[r]);
        }
        if (outside) {
            out.push_back(v);
        }
    }
    return out;
}

// Outside the corridors: patches of 32 x 32 samples 2 m apart and tiles of LOD 0 to 3 at the
// places, every bit of them.
void testOutsideCorridors(const World& w, Reaches& reaches) {
    const std::vector<terrain::RiverVertex> places = placesOutside(w, reaches, 24);
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, w.hydrology);
    golden::Checksum sum;
    std::vector<double> xs(32 * 32);
    std::vector<double> zs(xs.size());
    std::vector<terrain::HeightSample> samples(xs.size());
    for (std::size_t k = 0; k < places.size(); ++k) {
        const terrain::RiverVertex& v = places[k];
        for (int j = 0; j < 32; ++j) {
            for (int i = 0; i < 32; ++i) {
                xs[static_cast<std::size_t>(j * 32 + i)] = v.x - 31.0 + 2.0 * i;
                zs[static_cast<std::size_t>(j * 32 + i)] = v.z - 31.0 + 2.0 * j;
            }
        }
        w.field.samplesAt(xs, zs, samples);
        for (const terrain::HeightSample& s : samples) {
            sum.add(s.height);
            sum.add(static_cast<std::uint64_t>(s.water), 1);
            sum.add(s.waterLevel);
        }
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(v.x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(v.z / terrain::kChunkSizeMeters))};
        const terrain::TileData tile =
            tiles.generate({terrain::tileContaining(chunk, static_cast<int>(k % 4)), 0});
        golden::addMesh(sum, tile.mesh);
        golden::addMesh(sum, tile.water);
    }
    constexpr std::uint64_t kExpected = 0x6E105BE122CE3138ULL;
    std::printf("%zu places outside the corridors of the falls: "
                "{\"outside the falls\", 0x%016" PRIX64 "ULL}%s\n",
                places.size(), sum.value(), sum.value() == kExpected ? "" : "  (differs)");
    TEST_CHECK(places.size() >= 12);
    TEST_CHECK(sum.value() == kExpected);
}

// The drops of the water along a reach (more than kDrop meters between two points): each the
// last point of the pool above it whose slot is kDeep meters deep at least, and the first point
// below its face (its foot), where the slot is as deep.
constexpr float kDrop = 3.0f;
constexpr double kDeep = 4.0;

struct Drop {
    std::size_t above = 0;
    std::size_t below = 0;
    float height = 0.0f;
};

std::vector<Drop> dropsOf(const RiverReach& reach) {
    std::vector<Drop> out;
    const auto& p = reach.points;
    for (std::size_t i = 1; i + 2 < p.size(); ++i) {
        if (!(p[i].level - p[i + 1].level > kDrop)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 2 < p.size() && p[j].level - p[j + 1].level > 1.0f) {
            ++j;
        }
        std::size_t k = i - 1;
        while (k > 0 && p[k].level == p[i].level && depthAt(reach, k) < kDeep) {
            --k;
        }
        if (p[k].level == p[i].level && depthAt(reach, k) >= kDeep &&
            depthAt(reach, j + 1) >= kDeep) {
            out.push_back({k, j + 1, p[i].level - p[j].level});
        }
        i = j;
    }
    return out;
}

// No tooth along the slot: its rim does not stand farther from the axis below the drops than
// above them, kMeanJumpMost meters at most on average over every side of every drop, and the more
// so the taller the drop by kGrowthMost meters per meter of it at most (the least squares through
// 0). A slot as wide as it is deep widens at the foot of each step by about a quarter of its
// height: a tooth there. Its pools are oval (river_slot.hpp): narrower at the sill above each lip,
// wider in the bowl below each face, by shares of the channel, the longer bowls of the taller
// drops wider farther below their foot (about 0.05 m per meter of drop).
constexpr double kMeanJumpMost = 0.5;
constexpr double kGrowthMost = 0.1;

void testTeeth(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::size_t sides = 0;
    double jumps = 0.0;
    double byHeight = 0.0;
    double heights = 0.0;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        for (const Drop& d : dropsOf(reach)) {
            const std::array<double, 2> above = rimsAt(w, reach, d.above);
            const std::array<double, 2> below = rimsAt(w, reach, d.below);
            const auto h = static_cast<double>(d.height);
            for (std::size_t side = 0; side < 2; ++side) {
                const double jump = below[side] - above[side];
                jumps += jump;
                byHeight += jump * h;
                heights += h * h;
                ++sides;
            }
        }
    }
    const double mean = sides > 0 ? jumps / static_cast<double>(sides) : 1e9;
    const double growth = heights > 0.0 ? byHeight / heights : 1e9;
    std::printf("%zu sides of drops of falls: the rim of the slot %+.2f m farther from the axis "
                "below them than above on average, %+.3f m per meter of drop\n",
                sides, mean, growth);
    TEST_CHECK(sides >= 100);
    TEST_CHECK(mean <= kMeanJumpMost);
    TEST_CHECK(growth <= kGrowthMost);
}

// No slot vanishing at the lips of its steps (the last point of a pool above a face, steeper than
// kFace): below a floor coming down along the stretch as its steps do, a slot as deep at their
// feet as they are high would be as shallow as the floor at their lips (kFloorRise), its walls
// coming and going with each step (teeth again). The slot stays kLipLeast meters deep at the
// median lip at least.
constexpr double kFace = 1.0;
constexpr double kLipLeast = 1.5;

void testLips(Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> depths;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        for (std::size_t i = 1; i + 1 < p.size(); ++i) {
            const double drop = p[i].level - p[i + 1].level;
            const double run = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
            if (drop > kDrop && drop > kFace * run && p[i - 1].level == p[i].level) {
                depths.push_back(depthAt(reach, i));
            }
        }
    }
    const std::size_t lips = depths.size();
    const double median = quantile(depths, 0.5);
    std::printf("%zu lips of drops of falls: the slot %.1f m deep at the median one\n", lips,
                median);
    TEST_CHECK(lips >= 50);
    TEST_CHECK(median >= kLipLeast);
}

// No canal: along the falls the rim of the slot swings (its spread over its median at least
// kRimSpread on each side, the two sides apart by kSidesApart of it on average), and the steps
// are of all sizes (heights and runs between lips, their 90th percentile kStepSpread times their
// 10th at least).
constexpr double kRimSpread = 0.3;
constexpr double kSidesApart = 0.1;
constexpr double kStepSpread = 3.0;

void testVariety(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> rims;
    double apart = 0.0;
    std::vector<double> heights;
    std::vector<double> runs;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        for (std::size_t i = 0; i < reach.points.size(); i += 2) {
            if (depthAt(reach, i) >= kDeep) {
                const std::array<double, 2> rim = rimsAt(w, reach, i);
                rims.insert(rims.end(), rim.begin(), rim.end());
                apart += std::abs(rim[0] - rim[1]);
            }
        }
        double lip = -1.0;
        double along = 0.0;
        for (std::size_t i = 1; i < reach.points.size(); ++i) {
            const terrain::RiverPoint& a = reach.points[i - 1];
            const terrain::RiverPoint& b = reach.points[i];
            if (a.level - b.level > kDrop) {
                heights.push_back(static_cast<double>(a.level - b.level));
                if (lip >= 0.0) {
                    runs.push_back(along - lip);
                }
                lip = along;
            }
            along += std::hypot(b.x - a.x, b.z - a.z);
        }
    }
    const double median = quantile(rims, 0.5);
    const double spread = (quantile(rims, 0.9) - quantile(rims, 0.1)) / std::max(median, 1e-9);
    const double sides = apart / std::max(1.0, 0.5 * static_cast<double>(rims.size())) /
                         std::max(median, 1e-9);
    const double heightSpread = quantile(heights, 0.9) / std::max(quantile(heights, 0.1), 1e-9);
    const double runSpread = quantile(runs, 0.9) / std::max(quantile(runs, 0.1), 1e-9);
    std::printf("slots: rim %.1f m from the axis (median), spread %.2f of it, sides %.2f of it "
                "apart; steps: heights %.1f, runs %.1f from the 10th to the 90th percentile\n",
                median, spread, sides, heightSpread, runSpread);
    TEST_CHECK(rims.size() >= 100 && heights.size() >= 100);
    TEST_CHECK(spread >= kRimSpread && sides >= kSidesApart);
    TEST_CHECK(heightSpread >= kStepSpread && runSpread >= kStepSpread);
}

// The blocks of the pools: there are some; at their middle the ground stands at their top
// (within kOnTop: they never float, nor sink under another surface); over and around them the
// water never stands under the ground. On the LOD 0 tiles of the blocks out of the water, the
// vertices of the mesh over them lie on the ground and the mesh rises to their top, within a cell
// of the mesh (kMeshOnGround: the collision follows them, as finely as it follows any ground),
// and the surface blend of the tiles never puts water on their dry top.
constexpr double kOnTop = 0.05;
constexpr double kMeshOnGround = 1.0;

struct BlockChecks {
    std::size_t blocks = 0;
    std::size_t out = 0;   // their top above the water of their pool
    std::size_t wrong = 0; // a check failed
    std::size_t texels = 0; // dry tops classified
};

void checkBlockWater(const World& w, const terrain::RiverBlock& b, BlockChecks& c) {
    const terrain::HeightSample middle = w.field.sampleAt(b.x, b.z);
    c.wrong += std::abs(static_cast<double>(middle.height) - static_cast<double>(b.top)) > kOnTop;
    for (double dz = -2.0 * b.radius; dz <= 2.0 * b.radius; dz += 0.25) {
        for (double dx = -2.0 * b.radius; dx <= 2.0 * b.radius; dx += 0.25) {
            const terrain::HeightSample s = w.field.sampleAt(b.x + dx, b.z + dz);
            c.wrong += s.water != terrain::WaterKind::None && !(s.waterLevel > s.height);
        }
    }
}

void checkBlockTile(const terrain::TerrainTiles& tiles, const World& w,
                    const terrain::RiverBlock& b, BlockChecks& c) {
    const double x = terrain::wrapCoordinate(b.x, golden::kSmallPlanet.x);
    const double z = terrain::wrapCoordinate(b.z, golden::kSmallPlanet.z);
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    const terrain::TileData t = tiles.generate({terrain::tileContaining(chunk, 0), 0});
    double highest = -1e30;
    for (const terrain::Vec3f& v : t.mesh.vertices) {
        const double vx = t.originX + v.x;
        const double vz = t.originZ + v.z;
        if (std::hypot(vx - x, vz - z) < b.radius) {
            const double y = t.originY + static_cast<double>(v.y);
            highest = std::max(highest, y);
            c.wrong +=
                std::abs(y - static_cast<double>(w.field.heightAt(vx, vz))) > kMeshOnGround;
        }
    }
    c.wrong += highest < static_cast<double>(b.top) - kMeshOnGround;
    // The ground of its dry top as the surface blend of the tiles classifies it (at the pitch of
    // the texels of LOD 0): never water.
    const terrain::HeightmapRegion region{
        x, z, terrain::kTileBlendStride * terrain::tileCellMeters(0), 3};
    const terrain::EcologyMap map =
        tiles.ecology().map(terrain::surfaceFromHeightfield(tiles.heightfield(), region));
    ++c.texels;
    c.wrong += map.at(1, 1).material == terrain::SurfaceMaterial::Water;
}

void testBlocks(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, w.hydrology);
    BlockChecks c;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        for (const terrain::RiverBlock& b : reach.blocks) {
            if (c.blocks >= 120) {
                break;
            }
            ++c.blocks;
            checkBlockWater(w, b, c);
            const terrain::HeightSample middle = w.field.sampleAt(b.x, b.z);
            if (middle.water == terrain::WaterKind::None && c.out < 8) {
                ++c.out;
                checkBlockTile(tiles, w, b, c);
            }
        }
    }
    std::printf("%zu blocks in the pools of the falls, %zu out of the water checked on their "
                "tiles, %zu checks failed\n",
                c.blocks, c.out, c.wrong);
    TEST_CHECK(c.blocks >= 40 && c.out >= 4);
    TEST_CHECK(c.wrong == 0);
}

// The ends of the corridors of the falls: where a run of points every few meters (along the water
// where the slot of the falls winds) meets the course every 128 m before or after it. Along the
// axis kEndSpan meters on each side of each end, every kEndStep meters, within the valley of the
// river (riversAt: another may cross the axis), the water and the ground never jump: kWaterJump
// and kBedJump meters at most from a sample to the next (a slope of 0.4: the water comes down to
// the first lip of a stretch steeper than the river above it), but within kNearDrop meters of a
// drop of the water of the course (a fall itself). The same where two corridors lie within
// kNeighbors meters of each other (on one reach or across the end of a reach), and where a
// tributary ends within as many meters of one.
void testCorridorEnds(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<RunEnd> ends;
    std::size_t runs = 0;
    for (const std::size_t r : steeps) {
        endsOf(reaches[r], r, runs, ends);
    }
    // Neighbors: ends of two runs within kNeighbors meters; confluences: the last point of a
    // tributary (another reach ending on this one) within as many meters of an end.
    std::size_t neighbors = 0;
    std::size_t confluences = 0;
    std::vector<std::pair<std::size_t, std::size_t>> also; // reach, point
    for (std::size_t a = 0; a < ends.size(); ++a) {
        for (std::size_t b = a + 1; b < ends.size(); ++b) {
            neighbors += ends[a].run != ends[b].run &&
                         std::hypot(ends[a].x - ends[b].x, ends[a].z - ends[b].z) < kNeighbors;
        }
        for (const std::size_t r : w.hydrology->reachesNear(ends[a].x, ends[a].z, kNeighbors)) {
            const RiverReach& t = reaches[r];
            if (r != ends[a].reach && !t.points.empty() &&
                std::hypot(t.points.back().x - ends[a].x, t.points.back().z - ends[a].z) <
                    kNeighbors &&
                t.points.size() > 1) {
                ++confluences;
                also.emplace_back(r, t.points.size() - 1);
            }
        }
    }
    Jumps most;
    for (const RunEnd& e : ends) {
        const Jumps j = jumpsAround(w, reaches[e.reach], e.reach, e.point);
        most = {std::max(most.water, j.water), std::max(most.bed, j.bed)};
    }
    for (const auto& [r, point] : also) {
        const Jumps j = jumpsAround(w, reaches[r], r, point);
        most = {std::max(most.water, j.water), std::max(most.bed, j.bed)};
    }
    std::printf("%zu ends of %zu corridors of the falls, %zu pairs of them within %.0f m of each "
                "other, %zu tributaries ending as near: along their axes the water jumps %.3f m "
                "and the ground %.3f m at most every %.1f m\n",
                ends.size(), runs, neighbors, kNeighbors, confluences, most.water, most.bed,
                kEndStep);
    TEST_CHECK(runs >= 50);
    TEST_CHECK(most.water <= kWaterJump && most.bed <= kBedJump);
}

} // namespace

int main() {
    const World w = makeWorld();
    Reaches reaches(*w.hydrology);
    testOutsideCorridors(w, reaches);
    const std::vector<std::size_t> steeps = steepReaches(w, reaches);
    testTeeth(w, reaches, steeps);
    testLips(reaches, steeps);
    testVariety(w, reaches, steeps);
    testBlocks(w, reaches, steeps);
    testCorridorEnds(w, reaches, steeps);
    return test::result();
}
