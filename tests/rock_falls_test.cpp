// The faces of the rock of the slots of the falls (water/slot_faces.hpp, phase 12A) on the small
// world, on sections across the slots below the feet of its falls:
// - the walls the tiles of LOD 0 draw in full (kNotchWidth across) vertical: on kLeastVertical of
//   them at least, the rock begins within kVertical meters of one place at every meter of their
//   height, from above their notch to kRimRound below their rim;
// - a notch on every one of them kNotchFull meters high or more, on no wall below kNotchFrom;
//   never deeper than kNotchDeepSoft (3 m), so the tiles of LOD 1 (cells of 2 m) draw the walls
//   without it, to the bit;
// - deeper and higher in softer rock (slotFace), varied along the walls without repeating itself
//   (the detector of rock_patterns_test on long straight walls);
// - a walker (as rock_walk_test's) going into the lowest notch from in front of it and back out:
//   never stuck (no step it cannot climb back, never less than kHeadroom of air over it); the
//   notches show about 0.5 m deep at LOD 0 (slot_wall.hpp: the valleys hand the wall on only
//   half a meter behind the rim);
// - the walls of the gorges (heightfield_hydrology.cpp, gorgeFaces) with carved faces on
//   kLeastGorges of the sections across their deep stretches (gorge kGorgeDeep), none across the
//   open valleys (gorge below kGorgeOpen) kFarFromFalls meters from any fall.
#include "falls_support.hpp"
#include "pattern_support.hpp"
#include "slot_faces.hpp"

#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <map>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {

using terrain::RockColumn;
using terrain::RockFace;

constexpr double kHalfSection = 32.0;
constexpr double kColumn = 0.25;
constexpr double kAhead = 6.0; // the section below the foot of each fall
constexpr double kVertical = 0.5;
constexpr double kLeastVertical = 0.95;
constexpr double kRimRound = 2.0;
constexpr double kLeastBand = 3.0;
constexpr double kWallLength = 2000.0; // the long straight walls of testNoPeriod, 1 m steps
constexpr double kDetrend = 40.0;      // the swells slower than this left out (rock_patterns)
constexpr double kLeastLag = 8.0;      // the spacings checked: beyond the width of a notch
constexpr double kMostLag = 80.0;
constexpr double kIrregular = 0.5;
constexpr double kLeastVariation = 0.03;
constexpr double kWalkedNotch = 2.0; // the notch walked into: at least that deep
constexpr double kStep = 0.05;
constexpr double kStepMost = 0.45;
constexpr double kHeadroom = 2.0;
constexpr double kInFront = 0.75;
constexpr double kWalkMost = 6.0;
constexpr float kGorgeDeep = 0.7f;
constexpr float kGorgeOpen = 0.3f;
constexpr double kFarFromFalls = 300.0;
constexpr double kGorgeHalf = 120.0; // the sections across the gorges, every 2 m
constexpr std::size_t kGorgeEvery = 6;  // points of a reach between two of them
constexpr std::size_t kOpenEvery = 40;  // the same across the open valleys
constexpr double kLeastGorges = 0.3;

struct Section {
    double x = 0.0; // its middle and the unit direction across it
    double z = 0.0;
    double nx = 0.0;
    double nz = 0.0;
    std::vector<RockColumn> rock;
    [[nodiscard]] double at(std::size_t k) const {
        return -kHalfSection + static_cast<double>(k) * kColumn;
    }
};

Section sectionAt(const falls::World& w, double x, double z, double nx, double nz) {
    Section s{x, z, nx, nz, {}};
    std::vector<double> xs;
    std::vector<double> zs;
    for (double t = -kHalfSection; t <= kHalfSection + 1e-9; t += kColumn) {
        xs.push_back(x + nx * t);
        zs.push_back(z + nz * t);
    }
    std::vector<terrain::HeightSample> out(xs.size());
    s.rock.resize(xs.size());
    w.field.samplesAt(xs, zs, out, s.rock);
    return s;
}

const RockFace* carvedOf(const RockColumn& c) {
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        if (c.faces[i].carved) {
            return &c.faces[i];
        }
    }
    return nullptr;
}

// A wall of a section: the column nearest the middle of its face on one side.
struct Wall {
    std::size_t column = 0;
    RockFace face;
    double vertical = -1.0; // the spread of where the rock begins over its band (-1: no band)
};

std::optional<Wall> wallOn(const Section& s, int side) {
    const std::size_t mid = s.rock.size() / 2;
    std::optional<Wall> best;
    for (std::size_t k = mid; k < s.rock.size(); k = side > 0 ? k + 1 : k - 1) {
        const RockFace* f = carvedOf(s.rock[k]);
        if (f != nullptr && (!best || std::abs(f->distance) < std::abs(best->face.distance))) {
            best = Wall{k, *f, -1.0};
        }
        if (k == 0) {
            break;
        }
    }
    if (!best) {
        return best;
    }
    const terrain::RockDetail lod0 = terrain::rockDetailForLod(0);
    const RockFace& f = best->face;
    double lo = 1e9;
    double hi = -1e9;
    const double from = f.foot + std::max(1.0, static_cast<double>(f.undercutHeight) + 1.0);
    for (double y = from; y <= f.top - kRimRound; y += 1.0) {
        for (std::size_t k = mid; k < s.rock.size(); k = side > 0 ? k + 1 : k - 1) {
            if (terrain::rockDensity(s.rock[k], y, lod0) < 0.0f) {
                lo = std::min(lo, s.at(k) * side);
                hi = std::max(hi, s.at(k) * side);
                break;
            }
            if (k == 0) {
                break;
            }
        }
    }
    if (hi >= lo && f.top - kRimRound - from >= kLeastBand) {
        best->vertical = hi - lo;
    }
    return best;
}

// The undercut of LOD 1: none, to the bit (the density of a column the same without it).
bool lod1Unchanged(const RockColumn& c) {
    RockColumn bare = c;
    for (std::size_t i = 0; i < bare.faceCount; ++i) {
        bare.faces[i].undercut = 0.0f;
    }
    const terrain::RockDetail lod1 = terrain::rockDetailForLod(1);
    const RockFace* f = carvedOf(c);
    for (double y = f->foot - 1.0; y <= f->foot + f->undercutHeight + 1.0; y += 0.25) {
        if (terrain::rockDensity(c, y, lod1) != terrain::rockDensity(bare, y, lod1)) {
            return false;
        }
    }
    return true;
}

struct Counts {
    std::size_t walls = 0;
    std::size_t full = 0; // kNotchWidth across: drawn in full at LOD 0
    std::size_t banded = 0;
    std::size_t vertical = 0;
    std::size_t low = 0; // below kNotchFrom
    std::size_t lowNotched = 0;
    std::size_t high = 0; // kNotchFull or more
    std::size_t highNotched = 0;
    std::size_t lod1Changed = 0;
    double deepest = 0.0;
    std::optional<std::pair<Section, Wall>> lowest; // the lowest notch kWalkedNotch deep
};

void count(const Section& s, Counts& c) {
    for (const int side : {-1, 1}) {
        const std::optional<Wall> w = wallOn(s, side);
        if (!w) {
            continue;
        }
        const RockFace& f = w->face;
        const double height = static_cast<double>(f.top) - f.foot;
        const bool notched = f.undercut > 0.0f;
        ++c.walls;
        if (height < terrain::water::kNotchFrom) {
            ++c.low;
            c.lowNotched += notched ? 1U : 0U;
        }
        if (notched) {
            c.deepest = std::max(c.deepest, static_cast<double>(f.undercut));
            c.lod1Changed += lod1Unchanged(s.rock[w->column]) ? 0U : 1U;
        }
        if (!(f.run >= terrain::water::kNotchWidth)) {
            continue; // drawn in part at LOD 0 (rock_faces.hpp: kRunCells)
        }
        ++c.full;
        c.banded += w->vertical >= 0.0 ? 1U : 0U;
        c.vertical += w->vertical >= 0.0 && w->vertical <= kVertical ? 1U : 0U;
        if (height >= terrain::water::kNotchFull) {
            ++c.high;
            c.highNotched += notched ? 1U : 0U;
        }
        if (f.undercut >= kWalkedNotch &&
            (!c.lowest || f.undercutHeight < c.lowest->second.face.undercutHeight)) {
            c.lowest = std::make_pair(s, *w);
        }
    }
}

// slotFace along long straight walls of one hardness: its notch varies (kLeastVariation of its
// depth at least) without repeating itself (the detector of rock_patterns_test), whatever the
// direction of the wall.
void testNoPeriod() {
    terrain::SlotWall w;
    w.width = 8.0f;
    w.foot = 100.0f;
    w.top = 130.0f;
    w.water = 100.0f;
    w.airX = 1.0f;
    w.weight = 1.0f;
    double peak = 0.0;
    double least = 1e9;
    for (const double angle : {0.0, 0.7, 1.9, 2.6}) {
        std::vector<double> depths;
        std::vector<double> heights;
        for (double t = 0.0; t < kWallLength; t += 1.0) {
            const RockFace f = terrain::water::slotFace(w, 1000.0 + t * std::cos(angle),
                                                        -3000.0 + t * std::sin(angle), 0.5f,
                                                        golden::kSmallPlanet);
            depths.push_back(f.undercut);
            heights.push_back(f.undercutHeight);
        }
        for (const std::vector<double>* p : {&depths, &heights}) {
            peak = std::max(peak, pattern_test::peakCorrelation(
                                      pattern_test::detrended(*p,
                                                              static_cast<std::size_t>(kDetrend)),
                                      static_cast<std::size_t>(kLeastLag),
                                      static_cast<std::size_t>(kMostLag)));
        }
        least = std::min(least, pattern_test::variation(depths));
    }
    std::printf("the notches along straight walls of %.0f m: variation %.3f at least, peak %.2f\n",
                kWallLength, least, peak);
    TEST_CHECK(least >= kLeastVariation && peak < kIrregular);
}

// slotFace: a notch deeper and higher in soft rock than in hard rock, at the same place.
void testHardness() {
    terrain::SlotWall w;
    w.width = 8.0f;
    w.foot = 100.0f;
    w.top = 130.0f;
    w.water = 100.0f;
    w.airX = 1.0f;
    w.weight = 1.0f;
    std::size_t deeper = 0;
    std::size_t places = 0;
    for (double x = 0.0; x < 400.0; x += 7.3) {
        const RockFace soft = terrain::water::slotFace(w, x, 0.5 * x, 0.0f, golden::kSmallPlanet);
        const RockFace hard = terrain::water::slotFace(w, x, 0.5 * x, 1.0f, golden::kSmallPlanet);
        ++places;
        deeper += soft.undercut > hard.undercut && soft.undercutHeight >= hard.undercutHeight;
    }
    TEST_CHECK(deeper == places);
}

// The LOD 0 tiles of the small world, each built once.
class Tiles {
public:
    explicit Tiles(const terrain::TerrainTiles& tiles) : tiles_(tiles) {}
    const terrain::TileData& at(double x, double z) {
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
        const terrain::TileKey key = terrain::tileContaining(chunk, 0);
        const std::pair<std::int32_t, std::int32_t> k{key.origin.x, key.origin.z};
        auto it = built_.find(k);
        if (it == built_.end()) {
            it = built_.emplace(k, tiles_.generate({key, 0})).first;
        }
        return it->second;
    }

private:
    const terrain::TerrainTiles& tiles_;
    std::map<std::pair<std::int32_t, std::int32_t>, terrain::TileData> built_;
};

// The heights of the triangles of tile t over (x, z), sorted.
std::vector<double> surfacesAt(const terrain::TileData& t, double x, double z) {
    std::vector<double> out;
    const terrain::Mesh& m = t.mesh;
    const double px = x - t.originX;
    const double pz = z - t.originZ;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const terrain::Vec3f& a = m.vertices[m.indices[i]];
        const terrain::Vec3f& b = m.vertices[m.indices[i + 1]];
        const terrain::Vec3f& c = m.vertices[m.indices[i + 2]];
        const double d0 = (b.x - a.x) * (pz - a.z) - (b.z - a.z) * (px - a.x);
        const double d1 = (c.x - b.x) * (pz - b.z) - (c.z - b.z) * (px - b.x);
        const double d2 = (a.x - c.x) * (pz - c.z) - (a.z - c.z) * (px - c.x);
        const double area = d0 + d1 + d2;
        const bool inside =
            (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) || (d0 <= 0.0 && d1 <= 0.0 && d2 <= 0.0);
        if (inside && std::abs(area) > 1e-12) {
            out.push_back(t.originY + (d1 * a.y + d2 * b.y + d0 * c.y) / area);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// One step of a walker standing on floor at (x, z): the highest surface kStepMost above it at
// most and the air over it (false: blocked by the rock, a step too high or too little air).
bool stepTo(Tiles& tiles, double x, double z, double& floor, double& headroom) {
    const std::vector<double> surfaces = surfacesAt(tiles.at(x, z), x, z);
    std::optional<double> next;
    double above = 1e9;
    for (const double y : surfaces) {
        if (y <= floor + kStepMost) {
            next = y;
        } else {
            above = std::min(above, y);
        }
    }
    if (!next || above - *next < kHeadroom) {
        return false;
    }
    floor = *next;
    headroom = above - *next;
    return true;
}

// From kInFront meters in front of the face of the lowest notch into it as far as the walker
// goes, then back out to where it started: never stuck.
void walkIntoNotch(const falls::World& w, const Section& s, const Wall& wall) {
    const terrain::TerrainTiles field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, w.hydrology);
    Tiles tiles(field);
    const RockFace& f = wall.face;
    const double t = s.at(wall.column);
    // The middle of the face, and the way into the rock (against its air).
    const double mx = s.x + s.nx * t - f.airX * f.distance;
    const double mz = s.z + s.nz * t - f.airZ * f.distance;
    const double ix = -f.airX;
    const double iz = -f.airZ;
    double floor = f.foot + 1.0;
    double headroom = 0.0;
    double least = 1e9;
    bool started = false;
    double reached = -kInFront;
    for (double d = -kInFront; d <= kWalkMost; d += kStep) {
        const double before = floor;
        if (!stepTo(tiles, mx + ix * d, mz + iz * d, floor, headroom)) {
            break;
        }
        if (!started) {
            started = std::abs(floor - before) < 1.5;
        }
        least = std::min(least, headroom);
        reached = d;
    }
    bool out = started;
    for (double d = reached; out && d >= -kInFront; d -= kStep) {
        out = stepTo(tiles, mx + ix * d, mz + iz * d, floor, headroom);
        least = std::min(least, headroom);
    }
    std::printf("the lowest notch: %.1f m high, %.2f m deep; the walker went %.2f m into the rock "
                "from the face and back out: %s, %.1f m of air over it at least\n",
                f.undercutHeight, f.undercut, reached, out ? "never stuck" : "stuck", least);
    TEST_CHECK(started && out && reached > 0.0 && least >= kHeadroom);
}

// Sections across the valley of reach at its points of gorge in [low, high], one every every
// points, kFarFromFalls meters from its falls: those with carved faces, and all of them.
std::pair<std::size_t, std::size_t> gorgeSections(const falls::World& w,
                                                  const terrain::RiverReach& reach, float low,
                                                  float high, std::size_t every) {
    std::size_t faced = 0;
    std::size_t all = 0;
    for (std::size_t i = 1; i + 1 < reach.points.size(); i += every) {
        const terrain::RiverPoint& p = reach.points[i];
        const terrain::RiverPoint& q = reach.points[i + 1];
        const bool far = std::all_of(reach.falls.begin(), reach.falls.end(), [&](const auto& f) {
            return std::hypot(f.x - p.x, f.z - p.z) > kFarFromFalls;
        });
        const double length = std::hypot(q.x - p.x, q.z - p.z);
        if (p.gorge < low || p.gorge > high || !far || !(length > 0.0)) {
            continue;
        }
        const double nx = -(q.z - p.z) / length;
        const double nz = (q.x - p.x) / length;
        std::vector<double> xs;
        std::vector<double> zs;
        for (double t = -kGorgeHalf; t <= kGorgeHalf; t += 2.0) {
            xs.push_back(p.x + nx * t);
            zs.push_back(p.z + nz * t);
        }
        std::vector<terrain::HeightSample> out(xs.size());
        std::vector<RockColumn> rock(xs.size());
        w.field.samplesAt(xs, zs, out, rock);
        ++all;
        faced += std::any_of(rock.begin(), rock.end(),
                             [](const RockColumn& c) { return carvedOf(c) != nullptr; });
    }
    return {faced, all};
}

// The courses of the reaches falls::steepReaches looks at, computed on every core first (the
// hydrology keeps them): most of the time of the test.
void warmCourses(const falls::World& w) {
    std::vector<std::size_t> candidates;
    for (const auto& segment : w.planet->rivers().segments) {
        const auto& v = segment.vertices;
        for (std::size_t k = 0; k + 1 < v.size(); ++k) {
            if (v[k].elevation - v[k + 1].elevation >= falls::kSteepGraph) {
                for (const std::size_t r : w.hydrology->reachesNear(v[k].x, v[k].z, 1.0)) {
                    candidates.push_back(r);
                }
            }
        }
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::jthread> pool;
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < candidates.size(); i = next++) {
                static_cast<void>(w.hydrology->reach(candidates[i]));
            }
        });
    }
}

} // namespace

int main() {
    const falls::World w = falls::makeWorld();
    warmCourses(w);
    falls::Reaches reaches(*w.hydrology);
    Counts c;
    std::size_t gorgeFaced = 0;
    std::size_t gorgeAll = 0;
    std::size_t openFaced = 0;
    std::size_t openAll = 0;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        const auto [gf, ga] = gorgeSections(w, reaches[r], kGorgeDeep, 1.0f, kGorgeEvery);
        const auto [of, oa] = gorgeSections(w, reaches[r], 0.0f, kGorgeOpen, kOpenEvery);
        gorgeFaced += gf;
        gorgeAll += ga;
        openFaced += of;
        openAll += oa;
        for (const terrain::RiverFall& f : reaches[r].falls) {
            double dx = f.footX - f.x;
            double dz = f.footZ - f.z;
            const double length = std::hypot(dx, dz);
            if (length > 0.0) {
                dx /= length;
                dz /= length;
                count(sectionAt(w, f.footX + dx * kAhead, f.footZ + dz * kAhead, -dz, dx), c);
            }
        }
    }
    std::printf("slot walls: %zu with a face, %zu of them %.0f m across; of these, %zu of the %zu "
                "with %.0f m of band vertical within %.1f m, %zu from %.0f m high, %zu of them "
                "notched; below %.0f m %zu walls, %zu notched; deepest notch %.2f m, %zu changed "
                "at LOD 1\n",
                c.walls, c.full, terrain::water::kNotchWidth, c.vertical, c.banded, kLeastBand,
                kVertical, c.high, terrain::water::kNotchFull, c.highNotched,
                terrain::water::kNotchFrom, c.low, c.lowNotched, c.deepest, c.lod1Changed);
    TEST_CHECK(c.walls >= 100 && c.full >= 3 && c.banded >= 3);
    TEST_CHECK(static_cast<double>(c.vertical) >= kLeastVertical * static_cast<double>(c.banded));
    TEST_CHECK(c.low > 0 && c.lowNotched == 0);
    TEST_CHECK(c.high > 0 && c.highNotched == c.high);
    TEST_CHECK(c.deepest > 0.0 && c.deepest <= terrain::water::kNotchDeepSoft);
    TEST_CHECK(c.lod1Changed == 0);
    std::printf("gorges: %zu of the %zu sections across deep ones with carved faces, %zu of the %zu "
                "across open valleys\n",
                gorgeFaced, gorgeAll, openFaced, openAll);
    TEST_CHECK(gorgeAll >= 20 && static_cast<double>(gorgeFaced) >=
                                     kLeastGorges * static_cast<double>(gorgeAll));
    TEST_CHECK(openAll >= 20 && openFaced == 0);
    testNoPeriod();
    testHardness();
    TEST_CHECK(c.lowest.has_value());
    if (c.lowest) {
        walkIntoNotch(w, c.lowest->first, c.lowest->second);
    }
    return test::result();
}
