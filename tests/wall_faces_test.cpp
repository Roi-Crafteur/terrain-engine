// The faces of the walls of the mountains in three dimensions (phase 12A, wall_faces.hpp): the
// passes the faces read are those of rockTop bit for bit; across a synthetic wall the rock stands
// vertical at the middle of the run of its faces, the relief lying within their heights where it
// climbs them; the same faces through a memo and across the seams of the world; on the highest
// wall by the K2 the faces stand, some with caps or niches, the rock overhangs the air, and no rock
// floats; the joints never fold a face, and their grooves come in two kinds, most open and lit,
// some tight and dark.
#include "wall_faces.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::RockColumn;
using terrain::RockFace;
namespace walls = terrain::mountain_walls;

constexpr std::uint64_t kSeed = 0xA11ULL;
// The highest wall by the K2 (mountain_freeze_test's window "highest wall").
constexpr double kWallX = 416'000.0;
constexpr double kWallZ = -11'414'800.0;

walls::Column wallColumn(double ground, double gradX, double gradZ) {
    walls::Column c;
    c.ground = ground;
    c.gradX = gradX;
    c.gradZ = gradZ;
    c.intensity = 0.65f;
    c.character = 0.4f;
    c.relief = 1'500.0f;
    return c;
}

// The strata passes of wallPasses, chained as rockTop chains them, give its top bit for bit.
void testPassesAreRockTop() {
    const walls::Noises n = walls::makeNoises(kSeed, {});
    int compared = 0;
    bool same = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(4'000, 1e7)) {
        const walls::Column c = wallColumn(3'000.0 + 0.001 * p.x, 0.8, -0.6);
        const walls::Masks m = walls::masks(n, c, p.x, p.z, nullptr);
        if (!(m.strata > 0.0) || m.crystal > 0.0) {
            continue;
        }
        const walls::WallPasses w = walls::wallPasses(n, m, p.x, p.z, c.gradX, c.gradZ, nullptr);
        double h = c.ground;
        for (std::size_t i = 0; i < 2; ++i) {
            const walls::StrataPass& s = w.passes[i];
            h += s.weight *
                 (walls::strataTop(h, s.slope, s.band, s.offset, s.rim, s.run, s.bedding) - h);
        }
        same = same && h == walls::rockTop(n, m, p.x, p.z, c.ground, c.gradX, c.gradZ, nullptr);
        ++compared;
    }
    TEST_CHECK(compared > 100 && same);
}

const terrain::wall_faces::Grain& grain() {
    static const terrain::wall_faces::Grain g = terrain::wall_faces::makeGrain(kSeed, {});
    return g;
}

// The faces of a column of a synthetic wall at x (ground rising slope meters per meter along x),
// placed along that slope, their heights over the top of the walls there.
struct WallColumn {
    std::vector<RockFace> faces;
    double top = 0.0;
};

WallColumn columnAt(const walls::Noises& n, double x, double z, double slope) {
    const walls::Column c = wallColumn(3'000.0 + slope * x, slope, 0.0);
    WallColumn out;
    out.top = walls::surface(n, c, x, z, nullptr);
    const walls::Masks m = walls::masks(n, c, x, z, nullptr);
    terrain::wall_faces::Site site;
    site.faces = true;
    terrain::wall_faces::addFaces(n, grain(), m, c, x, z, nullptr, site);
    for (std::size_t i = 0; i < site.count; ++i) {
        RockFace f = terrain::wall_faces::placeFace(site.pending[i], slope, 0.0);
        f.foot += static_cast<float>(out.top);
        f.top += static_cast<float>(out.top);
        out.faces.push_back(f);
    }
    return out;
}

// A row of strata across which faces stand: each column lies within the run of its faces, the
// relief within their heights, the air downhill; the faces of one riser agree from column to
// column on where it stands.
void testSyntheticWall() {
    const walls::Noises n = walls::makeNoises(kSeed, {});
    // A place of strong strata.
    double z = 0.0;
    for (int k = 0; k < 2'000; ++k) {
        z = 1'000.0 * k;
        const walls::Masks m = walls::masks(n, wallColumn(3'000.0, 0.8, 0.0), 0.0, z, nullptr);
        if (m.strata > 0.9) {
            break;
        }
    }
    std::size_t faces = 0;
    bool within = true;
    double spread = 0.0;
    for (int i = -300; i <= 300; ++i) {
        const WallColumn c = columnAt(n, i, z, 0.8);
        faces += c.faces.empty() ? 0 : 1;
        for (const RockFace& f : c.faces) {
            within = within && std::abs(f.distance) <= 0.5f * f.run + 1e-3f &&
                     c.top >= f.foot - 0.5 && c.top <= f.top + 0.5 && f.airX < -0.99f;
            // The middle of the riser, x - distance along the air (-x): the same for its columns.
            const double middle = i + static_cast<double>(f.distance);
            const WallColumn next = columnAt(n, i + 1, z, 0.8);
            for (const RockFace& g : next.faces) {
                if (std::abs(g.top - g.foot - (f.top - f.foot)) < 0.5f) {
                    spread = std::max(spread, std::abs(i + 1 + static_cast<double>(g.distance) -
                                                       middle));
                }
            }
        }
    }
    std::printf("synthetic strata: %zu of 601 columns within a face, middles %.2f m apart\n",
                faces, spread);
    TEST_CHECK(faces > 20);
    TEST_CHECK(within);
    TEST_CHECK(spread < 1.0);
}

void testMemoAndSeams() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const terrain::WorldSize world{};
    std::vector<double> xs;
    std::vector<double> zs;
    for (int i = 0; i < 64; ++i) {
        xs.push_back(kWallX - 64.0 + 2.0 * i);
        zs.push_back(kWallZ + 3.0 * (i % 7));
    }
    std::vector<terrain::HeightSample> samples(xs.size());
    std::vector<RockColumn> rock(xs.size());
    field.samplesAt(xs, zs, samples, rock);
    bool same = true;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        same = same && rock[i] == field.rockAt(xs[i], zs[i]) &&
               rock[i] == field.rockAt(xs[i] + world.x, zs[i] - world.z) &&
               samples[i].height == field.heightAt(xs[i], zs[i]);
    }
    TEST_CHECK(same);
}

// The groove of a joint as a face of air (airX, airZ) draws it at (x, z), by placeFace.
double placedJoint(double x, double z, double airX, double airZ) {
    const terrain::wall_faces::Joint j = terrain::wall_faces::jointAt(grain(), x, z);
    terrain::wall_faces::PendingFace pending;
    pending.face.top = 30.0f;
    pending.face.joint = static_cast<float>(j.depth);
    pending.face.jointWidth = static_cast<float>(j.width);
    pending.jointFloor = j.floor;
    pending.rise = 30.0;
    pending.jointX = j.acrossX;
    pending.jointZ = j.acrossZ;
    pending.jointAlone = j.alone;
    pending.jointWall = j.wall;
    return terrain::wall_faces::placeFace(pending, -airX, -airZ).joint;
}

// The vertical joints are grooves within 0.4 of their width, with a floor; where one runs along a
// face (the worst case: the air across it), the face recedes into it slower than it stands back
// along its air (by kJointFold of it at most) where the joint runs alone: it never folds into a
// blade of rock between the groove and the air, which their own grooves would. Where another
// joint meets it, it goes on with the other's groove as it is, with no step.
void testJointsNeverFold() {
    std::size_t grooves = 0;
    double steepest = 0.0;
    double steepestAlone = 0.0;
    double steepestRaw = 0.0;
    bool shallow = true;
    for (int i = 0; i < 4'000; ++i) {
        const double x = kWallX + 0.37 * i;
        const double z = kWallZ + 0.53 * (i % 97);
        const terrain::wall_faces::Joint j = terrain::wall_faces::jointAt(grain(), x, z);
        shallow = shallow && j.depth <= j.floor && j.floor <= 0.4 * j.width + 1e-9;
        if (!(j.depth > 0.3)) {
            continue;
        }
        ++grooves;
        constexpr double kStep = 0.1;
        const double length = std::hypot(j.acrossX, j.acrossZ);
        const double airX = j.acrossX / length;
        const double airZ = j.acrossZ / length;
        const auto at = [&](double u) {
            return terrain::wall_faces::jointAt(grain(), x + u * airX, z + u * airZ);
        };
        for (double t = -4.0; t < 4.0; t += kStep) {
            const auto placed = [&](double u) {
                return placedJoint(x + u * airX, z + u * airZ, airX, airZ);
            };
            const double slope = std::abs(placed(t + kStep) - placed(t)) / kStep;
            steepest = std::max(steepest, slope);
            if (at(t).alone == 1.0 && at(t + kStep).alone == 1.0) {
                steepestAlone = std::max(steepestAlone, slope);
            }
            steepestRaw =
                std::max(steepestRaw, std::abs(at(t + kStep).depth - at(t).depth) / kStep);
        }
    }
    std::printf("joints: %zu points in a groove; across them a face along it recedes %.2f m per "
                "meter at most where they run alone, %.2f anywhere (%.2f without the limit)\n",
                grooves, steepestAlone, steepest, steepestRaw);
    TEST_CHECK(grooves > 100);
    TEST_CHECK(shallow);
    TEST_CHECK(steepestAlone < 0.9);
    TEST_CHECK(steepest <= steepestRaw + 0.1);
    TEST_CHECK(steepestRaw > 1.0);
}

// The share of the width of a groove (depth every kAcross meters across it, where it passes a
// twentieth of its floor) lit by a sun across it at kGrazing radians from the face, from the side
// of the samples after (side 1) or before (-1): not shaded by the rock of another sample.
constexpr double kAcross = 0.05;
constexpr double kGrazing = 20.0 * 3.14159265358979323846 / 180.0;
double litShare(const std::vector<double>& depth, double floor, int side) {
    const double rise = std::tan(kGrazing) * kAcross;
    const auto n = static_cast<int>(depth.size());
    std::size_t lit = 0;
    std::size_t count = 0;
    for (int u = 0; u < n; ++u) {
        if (!(depth[static_cast<std::size_t>(u)] > 0.05 * floor)) {
            continue;
        }
        ++count;
        bool shaded = false;
        for (int v = u + side; v >= 0 && v < n && !shaded; v += side) {
            shaded = depth[static_cast<std::size_t>(u)] - depth[static_cast<std::size_t>(v)] >
                     std::abs(v - u) * rise;
        }
        lit += shaded ? 0 : 1;
    }
    return count > 0 ? static_cast<double>(lit) / static_cast<double>(count) : 1.0;
}

// The grooves a line along the wall crosses, each across itself at its deepest where it runs
// alone: about one in five tight (0.4 of its width deep at most), the others open (a quarter of
// it at most). Under a sun grazing the face (20 degrees) from either side, no open groove is black
// (less than a tenth of its width lit), its far side lit over 30% of it at least, and the tight
// ones are darker: never a wall of identical grooves. Each joint keeps its seed along it (its
// stretches along the height, RockFace), one of its own (as many seeds as joints crossed, about).
void testGrooveKinds() {
    std::size_t crossed = 0;
    std::size_t tight = 0;
    std::size_t black = 0;
    std::size_t blackOpen = 0;
    double leastOpenLit = 1.0;
    std::vector<double> seeds;
    bool sameAlong = true; // the seed of a joint, up to 1 m along it either way in its groove
    std::array<double, 2> litSum{}; // open, tight
    bool shallow = true;
    double previous = 0.0;
    double deepest = 0.0;
    std::array<double, 2> at{};
    for (double t = 0.0; t < 4'000.0; t += 0.25) {
        const double x = kWallX + 0.955 * t;
        const double z = kWallZ + 0.296 * t;
        const double d = terrain::wall_faces::jointAt(grain(), x, z).depth;
        if (d > deepest) {
            deepest = d;
            at = {x, z};
        }
        if (d > 0.0 || !(previous > 0.0)) {
            previous = d;
            continue;
        }
        previous = d;
        const terrain::wall_faces::Joint j = terrain::wall_faces::jointAt(grain(), at[0], at[1]);
        deepest = 0.0;
        if (j.alone < 1.0 || !(j.floor > 0.1)) {
            continue;
        }
        const double length = std::hypot(j.acrossX, j.acrossZ);
        std::vector<double> profile;
        for (double u = -j.width; u <= j.width; u += kAcross) {
            profile.push_back(terrain::wall_faces::jointAt(grain(), at[0] + u * j.acrossX / length,
                                                           at[1] + u * j.acrossZ / length)
                                  .depth);
        }
        const double lit = std::min(litShare(profile, j.floor, 1), litShare(profile, j.floor, -1));
        const bool isTight = j.wall > 5.0;
        ++crossed;
        tight += isTight ? 1 : 0;
        black += lit < 0.1 ? 1 : 0;
        litSum[isTight ? 1 : 0] += lit;
        if (!isTight) {
            blackOpen += lit < 0.1 ? 1 : 0;
            leastOpenLit = std::min(leastOpenLit, lit);
        }
        shallow = shallow && j.floor <= (isTight ? 0.4 : 0.25) * j.width + 1e-9;
        seeds.push_back(j.seed);
        for (const double v : {-1.0, -0.5, 0.5, 1.0}) {
            const terrain::wall_faces::Joint o = terrain::wall_faces::jointAt(
                grain(), at[0] - v * j.acrossZ / length, at[1] + v * j.acrossX / length);
            // Within its groove (past a corner of its cells, another joint).
            const bool along = o.alone == 1.0 && o.depth > 0.5 * j.depth;
            sameAlong = sameAlong && (!along || o.seed == j.seed);
        }
    }
    const double tightShare = static_cast<double>(tight) / static_cast<double>(crossed);
    const double blackShare = static_cast<double>(black) / static_cast<double>(crossed);
    const double openLit = litSum[0] / static_cast<double>(crossed - tight);
    const double tightLit = litSum[1] / static_cast<double>(std::max<std::size_t>(1, tight));
    std::printf("grooves: %zu crossed, %.0f%% of them tight; under a grazing sun %.0f%% black (%zu "
                "open ones); lit over %.0f%% of their width on average (open), %.0f%% (tight), an "
                "open one over %.0f%% at least\n",
                crossed, 100.0 * tightShare, 100.0 * blackShare, blackOpen, 100.0 * openLit,
                100.0 * tightLit, 100.0 * leastOpenLit);
    TEST_CHECK(crossed > 50);
    TEST_CHECK(shallow);
    TEST_CHECK(tightShare > 0.1 && tightShare < 0.35);
    TEST_CHECK(blackOpen == 0 && leastOpenLit > 0.3);
    TEST_CHECK(tightLit < openLit - 0.1);
    std::sort(seeds.begin(), seeds.end());
    const auto distinct = static_cast<std::size_t>(
        std::unique(seeds.begin(), seeds.end()) - seeds.begin());
    std::printf("their seeds: %zu distinct of %zu, the same along each\n", distinct, crossed);
    TEST_CHECK(sameAlong && distinct * 10 >= crossed * 9);
}

} // namespace

int main() {
    testPassesAreRockTop();
    testSyntheticWall();
    testMemoAndSeams();
    testJointsNeverFold();
    testGrooveKinds();
    return test::result();
}
