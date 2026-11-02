// The shapes of the inlets and reefs (agent 33, commits 4 bis and 4 ter, after the checks in
// Unreal of the stop points 2 and 2 bis), on the default world:
// - The fjords: wide valleys in U between massifs. Across their axis at their site: water at least
//   kFjordWater wide, the valley at least kFjordValley wide kFjordRelief meters up and both sides
//   rising kFjordRelief meters within kFjordReach meters of the water for most of them. Their
//   flanks steep down to the water along their length (across it every kFjordStep meters from
//   their site to their head): over their first 200 m, kFjordBank steep at least for kFjordBanks
//   of them (no low green bank); never a wall: over 100 m, their slope at the 90th percentile at
//   most kFjordSteepest, in the middle of the fjords at most kFjordMedian.
// - The coves: on the coasts that rise (never a low coast at their mouth, the ground kCoveTop
//   meters up within kCoveReach meters of their site), opening out to the sea (their mouth at least
//   kCoveMouth of their widest water inland, for kCoveOpen of them at least).
// - The reefs: rare (off some capes only), groups of two to four low rocks (kRockLeast to kRockMost
//   meters high), about one in ten standing a stack (20 to 60 m), never one round dome alone; in
//   the tiles of LOD 0 to 3, no hole (no open edge of the mesh but on the sides of the tile), with
//   their rock in three dimensions and without it, and without it no teeth on a rock at the LODs
//   where it stands two cells high (as the faces of coast_patterns_test: the rocks in two
//   dimensions, as the tiles draw them where their faces are not drawn and the horizon samples
//   them; coast_rock_test checks their faces).
#include "terrain/chunk_coord.hpp"
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_teeth_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <tuple>
#include <vector>

namespace {

using terrain::CoastKind;
using terrain::CoastSite;
using terrain::InletKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

const terrain::TerrainTiles& tiles() {
    static const terrain::TerrainTiles t(kSeed);
    return t;
}

// The tiles of the relief alone (no rock in three dimensions): the rocks in two dimensions, as the
// tiles draw them where their faces are not drawn (the LOD too coarse) and as the horizon samples
// them; their faces in three dimensions have their own checks (coast_rock_test).
const terrain::TerrainTiles& reliefTiles() {
    static const terrain::TerrainTiles t(kSeed, {}, {}, std::nullopt, {}, nullptr, false);
    return t;
}

const terrain::Heightfield& field() {
    return tiles().heightfield();
}

double signalAt(double x, double z) {
    return field().coastAt(x, z).signal;
}

double heightAt(double x, double z) {
    return static_cast<double>(field().heightAt(x, z));
}

// The value of v at the share p of its sorted values.
double quantile(std::vector<double> v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(p * static_cast<double>(v.size()));
    return v[std::min(v.size() - 1, k)];
}

// Fjords: kFjords of them, the first by x then z 300 km around the coasts of the fjords.
constexpr std::array<std::array<double, 2>, 2> kFjordCoasts{
    {{3'624'471.0, -10'006'796.0}, {1'793'548.0, 9'821'452.0}}};
constexpr std::size_t kFjords = 24;
constexpr double kFjordWater = 1'000.0;
constexpr double kFjordValley = 2'000.0;
constexpr double kFjordRelief = 300.0;
constexpr double kFjordReach = 3'000.0;
constexpr double kFjordSteepest = 4.0;
constexpr double kFjordMedian = 2.0;
// The share of the fjords whose valley and both sides pass kFjordValley and kFjordRelief.
constexpr double kFjordMost = 0.6;
constexpr double kFjordStep = 500.0;
constexpr int kFjordSections = 4;
constexpr double kFjordBank = 0.7;
constexpr double kFjordBanks = 0.75;

// How far the water runs from (x, z) toward angle t, radians (6 km at most).
double waterRun(double x, double z, double t) {
    double d = 0.0;
    while (d < 6'000.0 &&
           signalAt(x + std::cos(t) * (d + 25.0), z + std::sin(t) * (d + 25.0)) < 0.0) {
        d += 25.0;
    }
    return d;
}

// A side of the cross-section of a fjord: its shore, where it reaches kFjordRelief meters, its
// highest within kFjordReach meters of the water, the 90th percentile of its slope over 100 m
// from the water to there, and its slope over its first 200 m.
struct Bank {
    double shore = 0.0;
    double up = 1e9;
    double highest = -1e9;
    double steep = 0.0;
    double first = 0.0;
};

Bank bankOf(double x, double z, double nx, double nz) {
    Bank b;
    while (b.shore < 4'000.0 && signalAt(x + nx * b.shore, z + nz * b.shore) < 0.0) {
        b.shore += 10.0;
    }
    double at = b.shore;
    for (double d = b.shore; d <= b.shore + kFjordReach; d += 25.0) {
        const double h = heightAt(x + nx * d, z + nz * d);
        b.up = h >= kFjordRelief ? std::min(b.up, d) : b.up;
        if (h > b.highest) {
            b.highest = h;
            at = d;
        }
    }
    std::vector<double> slopes;
    for (double d = b.shore; d + 100.0 <= std::max(at, b.shore + 200.0); d += 25.0) {
        slopes.push_back(std::abs(heightAt(x + nx * (d + 100.0), z + nz * (d + 100.0)) -
                                  heightAt(x + nx * d, z + nz * d)) /
                         100.0);
    }
    b.steep = quantile(slopes, 0.9);
    b.first = heightAt(x + nx * (b.shore + 200.0), z + nz * (b.shore + 200.0)) / 200.0;
    return b;
}

// The slopes over their first 200 m of the banks of a fjord across it, every kFjordStep meters
// from (x, z) up the coast signal of the geography while in its water inland of its mouth.
void addBanks(double x, double z, std::vector<double>& banks) {
    for (int k = 0; k < kFjordSections; ++k) {
        constexpr double h = 30.0;
        const double gx = field().coastAt(x + h, z).base - field().coastAt(x - h, z).base;
        const double gz = field().coastAt(x, z + h).base - field().coastAt(x, z - h).base;
        const double g = std::hypot(gx, gz);
        if (!(g > 0.0) || signalAt(x, z) >= 0.0 || field().coastAt(x, z).base < 0.0) {
            return;
        }
        const double nx = -gz / g;
        const double nz = gx / g;
        const Bank l = bankOf(x, z, nx, nz);
        const Bank r = bankOf(x, z, -nx, -nz);
        banks.push_back(l.first);
        banks.push_back(r.first);
        // Back to the middle of the water across, then up the fjord.
        x += 0.5 * (l.shore - r.shore) * nx + kFjordStep * gx / g;
        z += 0.5 * (l.shore - r.shore) * nz + kFjordStep * gz / g;
    }
}

void testFjords() {
    std::vector<CoastSite> fjords;
    for (const auto& [cx, cz] : kFjordCoasts) {
        for (const CoastSite& s : field().inletsIn(cx - 3e5, cz - 3e5, cx + 3e5, cz + 3e5)) {
            if (s.inlet == InletKind::Fjord && fjords.size() < kFjords) {
                fjords.push_back(s);
            }
        }
    }
    TEST_CHECK(fjords.size() == kFjords);
    std::vector<double> steep;
    std::vector<double> banks;
    int wide = 0;
    int most = 0;
    for (const CoastSite& s : fjords) {
        // The axis: the direction of the longest water through the site.
        double best = -1.0;
        double axis = 0.0;
        for (int a = 0; a < 36; ++a) {
            const double t = a * std::numbers::pi / 36.0;
            const double run = waterRun(s.x, s.z, t) + waterRun(s.x, s.z, t + std::numbers::pi);
            if (run > best) {
                best = run;
                axis = t;
            }
        }
        const Bank l = bankOf(s.x, s.z, -std::sin(axis), std::cos(axis));
        const Bank r = bankOf(s.x, s.z, std::sin(axis), -std::cos(axis));
        const double valley = l.up + r.up;
        const double low = std::min(l.highest, r.highest);
        const double worst = std::max(l.steep, r.steep);
        std::printf("fjord (%.0f, %.0f): water %.0f m, valley %.0f m at %.0f m, sides up to "
                    "%.0f / %.0f m, slope p90 %.2f / %.2f\n",
                    s.x, s.z, l.shore + r.shore, valley < 1e8 ? valley : -1.0, kFjordRelief,
                    l.highest, r.highest, l.steep, r.steep);
        wide += l.shore + r.shore >= kFjordWater ? 1 : 0;
        most += valley >= kFjordValley && valley < 1e8 && low >= kFjordRelief ? 1 : 0;
        steep.push_back(worst);
        addBanks(s.x, s.z, banks);
    }
    const double n = static_cast<double>(fjords.size());
    const auto steepBanks = std::count_if(banks.begin(), banks.end(),
                                          [](double b) { return b >= kFjordBank; });
    std::printf("banks across the fjords: %zu, %td of them at least %.1f steep over their first "
                "200 m (median %.2f, 10th percentile %.2f)\n",
                banks.size(), steepBanks, kFjordBank, quantile(banks, 0.5), quantile(banks, 0.1));
    std::printf("%zu fjords: %d with water %.0f m wide at least, %d between sides %.0f m high "
                "(valley %.0f m wide at least); steeper side, slope p90: median %.2f, most %.2f\n",
                fjords.size(), wide, kFjordWater, most, kFjordRelief, kFjordValley,
                quantile(steep, 0.5), quantile(steep, 1.0));
    TEST_CHECK(wide == static_cast<int>(fjords.size()));
    TEST_CHECK(static_cast<double>(most) >= kFjordMost * n);
    TEST_CHECK(quantile(steep, 0.5) <= kFjordMedian);
    TEST_CHECK(quantile(steep, 1.0) <= kFjordSteepest);
    TEST_CHECK(!banks.empty() &&
               static_cast<double>(steepBanks) >= kFjordBanks * static_cast<double>(banks.size()));
}

// Coves and stacks: near the coasts of rocky shores and cliffs of the tests of the stop points.
constexpr std::array<std::array<double, 2>, 3> kShoreCoasts{{{23'433'101.0, -2'128'828.0},
                                                              {-15'720'496.0, -6'266'783.0},
                                                              {-26'486'937.0, 7'219'391.0}}};
constexpr std::size_t kCoves = 60;
constexpr double kCoveTop = 8.0;
constexpr double kCoveReach = 500.0;
constexpr double kCoveMouth = 0.75;
constexpr double kCoveOpen = 0.8;

void testCoves() {
    int found = 0;
    int low = 0;
    int flat = 0;
    int open = 0;
    for (const auto& [cx, cz] : kShoreCoasts) {
        for (const CoastSite& s : field().inletsIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4)) {
            if (s.inlet != InletKind::Cove || found >= static_cast<int>(kCoves)) {
                continue;
            }
            ++found;
            low += s.coast == CoastKind::Low ? 1 : 0;
            double top = -1e9;
            for (int a = 0; a < 16; ++a) {
                const double t = a * std::numbers::pi / 8.0;
                for (const double r : {0.5 * kCoveReach, 0.75 * kCoveReach, kCoveReach}) {
                    top = std::max(top, heightAt(s.x + r * std::cos(t), s.z + r * std::sin(t)));
                }
            }
            flat += top < kCoveTop ? 1 : 0;
            if (top < kCoveTop || s.opening < kCoveMouth * s.size) {
                std::printf("  cove (%.0f, %.0f) at a %s coast: opening %.0f m, widest %.0f m, "
                            "ground up to %.1f m within %.0f m\n",
                            s.x, s.z, terrain::coastKindName(s.coast), s.opening, s.size, top,
                            kCoveReach);
            }
            open += s.opening >= kCoveMouth * s.size ? 1 : 0;
        }
    }
    std::printf("%d coves: %d at a low coast, %d on ground under %.0f m within %.0f m, %d with "
                "a mouth %.0f %% of their widest at least\n",
                found, low, flat, kCoveTop, kCoveReach, open, 100.0 * kCoveMouth);
    TEST_CHECK(found == static_cast<int>(kCoves));
    TEST_CHECK(low == 0);
    TEST_CHECK(flat == 0);
    TEST_CHECK(static_cast<double>(open) >= kCoveOpen * found);
}

// The reefs: their rocks (Heightfield::stacksIn) joined into reefs where kReefJoin meters apart
// at most; the ground in their middle kRockLeast to kRockMost meters high (domes of 3 to 12 m and
// the detail of the land on them), but for the stacks (kStackLeast meters high and more, their
// ground kStackLow to kStackHigh meters high: 20 to 60 m and their rough relief) and the rocks
// merged into them (within the reach of their lobes); at most kReefShare reefs per cove (rare);
// their first rock irregular (its reach above the water from its middle varying by kLobes at
// least).
constexpr double kReefJoin = 160.0;
constexpr double kRockLeast = 1.0;
constexpr double kRockMost = 16.0;
constexpr float kStackLeast = 20.0f;
constexpr double kStackLow = 16.0;
constexpr double kStackHigh = 64.0;
constexpr double kReefShare = 0.3;
constexpr double kLobes = 1.15;

// The rocks gathered into reefs (each rock to the first reef it lies near).
std::vector<std::vector<terrain::CoastStack>> reefsOf(
    const std::vector<terrain::CoastStack>& rocks) {
    std::vector<std::vector<terrain::CoastStack>> reefs;
    for (const terrain::CoastStack& r : rocks) {
        auto near = std::find_if(reefs.begin(), reefs.end(), [&r](const auto& reef) {
            return std::any_of(reef.begin(), reef.end(), [&r](const terrain::CoastStack& o) {
                return std::hypot(o.x - r.x, o.z - r.z) <= kReefJoin;
            });
        });
        if (near == reefs.end()) {
            reefs.push_back({r});
        } else {
            near->push_back(r);
        }
    }
    return reefs;
}

// How far above the water a rock reaches from its middle, longest over shortest of 16 directions.
double lobesOf(const terrain::CoastStack& r) {
    double most = 0.0;
    double least = 1e9;
    for (int a = 0; a < 16; ++a) {
        const double t = a * std::numbers::pi / 8.0;
        double d = 0.0;
        while (d < 3.0 * static_cast<double>(r.radius) &&
               heightAt(r.x + std::cos(t) * (d + 0.5), r.z + std::sin(t) * (d + 0.5)) > 0.0) {
            d += 0.5;
        }
        most = std::max(most, d);
        least = std::min(least, d);
    }
    return least > 0.0 ? most / least : 0.0;
}

// Whether a rock lies within the reach of the lobes of a stack of its reef (merged into it).
bool mergedIntoStack(const terrain::CoastStack& r, const std::vector<terrain::CoastStack>& reef) {
    for (const terrain::CoastStack& s : reef) {
        if (s.height >= kStackLeast &&
            std::hypot(r.x - s.x, r.z - s.z) < 1.4 * static_cast<double>(s.radius + r.radius)) {
            return true;
        }
    }
    return false;
}

void testReefs() {
    int reefCount = 0;
    int alone = 0;
    int wrongHeight = 0;
    int round = 0;
    int coves = 0;
    int rocksSeen = 0;
    int stacks = 0;
    for (const auto& [cx, cz] : kShoreCoasts) {
        const std::vector<terrain::CoastStack> rocks =
            field().stacksIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4);
        for (const CoastSite& s : field().inletsIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4)) {
            coves += s.inlet == InletKind::Cove ? 1 : 0;
        }
        for (const auto& reef : reefsOf(rocks)) {
            ++reefCount;
            alone += reef.size() < 2 ? 1 : 0;
            round += lobesOf(reef.front()) < kLobes ? 1 : 0;
            for (const terrain::CoastStack& r : reef) {
                const double h = heightAt(r.x, r.z);
                const bool stack = r.height >= kStackLeast;
                stacks += stack ? 1 : 0;
                if (!stack && mergedIntoStack(r, reef)) {
                    ++rocksSeen;
                    continue;
                }
                if (stack ? (h < kStackLow || h > kStackHigh) : (h < kRockLeast || h > kRockMost)) {
                    ++wrongHeight;
                    std::printf("  rock (%.0f, %.0f): %.1f m high drawn, ground %.1f m\n", r.x,
                                r.z, static_cast<double>(r.height), h);
                }
                ++rocksSeen;
            }
        }
    }
    std::printf("%d reefs (%d rocks, %d stacks) for %d coves: %d of a single rock, %d rocks out "
                "of %.0f to %.1f m (the stacks %.0f to %.0f m), %d first rocks round\n",
                reefCount, rocksSeen, stacks, coves, alone, wrongHeight, kRockLeast, kRockMost,
                kStackLow, kStackHigh, round);
    TEST_CHECK(reefCount > 0);
    TEST_CHECK(static_cast<double>(reefCount) <= kReefShare * coves);
    TEST_CHECK(alone == 0);
    TEST_CHECK(wrongHeight == 0);
    TEST_CHECK(round == 0);
}

// The open edges of a mesh off the sides of its box (its vertices welded where they meet).
int holesOf(const terrain::Mesh& m) {
    if (m.vertices.empty()) {
        return 0;
    }
    using Key = std::tuple<long long, long long, long long>;
    const auto key = [&m](std::uint32_t v) {
        const terrain::Vec3f& p = m.vertices[v];
        return Key{std::llround(p.x * 1e3), std::llround(p.y * 1e3), std::llround(p.z * 1e3)};
    };
    float x0 = m.vertices[0].x, x1 = x0, z0 = m.vertices[0].z, z1 = z0;
    for (const terrain::Vec3f& p : m.vertices) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
    }
    std::map<std::pair<Key, Key>, int> edges;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        for (std::size_t e = 0; e < 3; ++e) {
            const Key a = key(m.indices[t + e]);
            const Key b = key(m.indices[t + (e + 1) % 3]);
            ++edges[a < b ? std::pair{a, b} : std::pair{b, a}];
        }
    }
    const auto side = [&](const Key& k) {
        const double x = static_cast<double>(std::get<0>(k)) * 1e-3;
        const double z = static_cast<double>(std::get<2>(k)) * 1e-3;
        return std::abs(x - x0) < 1e-2 || std::abs(x - x1) < 1e-2 || std::abs(z - z0) < 1e-2 ||
               std::abs(z - z1) < 1e-2;
    };
    int holes = 0;
    for (const auto& [edge, count] : edges) {
        holes += count == 1 && !(side(edge.first) && side(edge.second)) ? 1 : 0;
    }
    return holes;
}

// The reefs in the tiles of LOD 0 to 3: the first rock of kTiledReefs reefs.
constexpr std::size_t kTiledReefs = 6;

void testReefTiles() {
    std::vector<terrain::CoastStack> firsts;
    for (const auto& [cx, cz] : kShoreCoasts) {
        const auto rocks = field().stacksIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4);
        for (const auto& reef : reefsOf(rocks)) {
            if (firsts.size() < kTiledReefs) {
                firsts.push_back(reef.front());
            }
        }
    }
    int holes = 0;
    int teeth = 0;
    int checked = 0;
    for (const terrain::CoastStack& r : firsts) {
        for (int lod = 0; lod < 4; ++lod) {
            const double c = terrain::tileCellMeters(lod);
            const terrain::ChunkCoord chunk{
                static_cast<std::int32_t>(std::floor(r.x / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor(r.z / terrain::kChunkSizeMeters))};
            holes += holesOf(tiles().generate({terrain::tileContaining(chunk, lod), 0}).mesh);
            const terrain::TileData data =
                reliefTiles().generate({terrain::tileContaining(chunk, lod), 0});
            holes += holesOf(data.mesh);
            if (static_cast<double>(r.height) < 2.0 * c) {
                continue; // a rock under two cells high: drawn as a bump, not checked
            }
            const coast_teeth::Gradient gradient = [c](double px, double pz) {
                return std::array<double, 2>{
                    (heightAt(px + c, pz) - heightAt(px - c, pz)) / (2.0 * c),
                    (heightAt(px, pz + c) - heightAt(px, pz - c)) / (2.0 * c)};
            };
            // The vertices of the rock alone (within twice its radius of its middle).
            terrain::Mesh rock;
            for (std::size_t v = 0; v < data.mesh.vertices.size(); ++v) {
                const double vx = data.originX + data.mesh.vertices[v].x - r.x;
                const double vz = data.originZ + data.mesh.vertices[v].z - r.z;
                if (std::hypot(vx, vz) < 2.0 * static_cast<double>(r.radius)) {
                    rock.vertices.push_back(data.mesh.vertices[v]);
                    rock.normals.push_back(data.mesh.normals[v]);
                }
            }
            coast_teeth::Teeth t;
            coast_teeth::addMesh(t, rock, {data.originX, data.originY, data.originZ}, 1.0, c,
                                 gradient, 0.3);
            const coast_teeth::Errors e = coast_teeth::errorsOf(t, c);
            ++checked;
            if (!coast_teeth::clean(e)) {
                ++teeth;
                std::printf("  rock (%.0f, %.0f) %.1f m, LOD %d: %.1f/%.1f/%.1f\n", r.x, r.z,
                            static_cast<double>(r.height), lod, e.rms, e.p95, e.periodic);
            }
        }
    }
    std::printf("reefs in the tiles of LOD 0 to 3: %zu reefs, %d holes, %d tiles with teeth of %d "
                "checked\n",
                firsts.size(), holes, teeth, checked);
    TEST_CHECK(!firsts.empty());
    TEST_CHECK(holes == 0);
    TEST_CHECK(teeth == 0);
}

} // namespace

int main() {
    testFjords();
    testCoves();
    testReefs();
    testReefTiles();
    return test::result();
}
