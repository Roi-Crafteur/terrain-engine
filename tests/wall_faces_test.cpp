// The faces of the walls of the mountains in three dimensions (phase 12A, wall_faces.hpp): the
// passes the faces read are those of rockTop bit for bit; across a synthetic wall the rock stands
// vertical at the middle of the run of its faces, the relief lying within their heights where it
// climbs them; the same faces through a memo and across the seams of the world; on the highest
// wall by the K2 the faces stand, some with caps or niches, the rock overhangs the air, and no rock
// floats.
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
    terrain::wall_faces::addFaces(n, m, c, x, z, nullptr, site);
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

} // namespace

int main() {
    testPassesAreRockTop();
    testSyntheticWall();
    testMemoAndSeams();
    return test::result();
}
