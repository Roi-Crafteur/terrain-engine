// The faces of the coasts as a renderer lights them beyond the tiles of LOD 0 (agent 33, after the
// check in Unreal of c051ba7: rows of light and dark triangles on the cliffs seen from 500 and
// 800 m, white spikes at the foot of the wall of a fjord), on the default world with its planet
// file, at the places of coast_rock_support.hpp. Along the line of each face (traced on the
// distance of its columns to the plane of their face, every kStep meters over 2 kHalf meters):
// - The normals of the level 0 of the horizon, as its material reads them (bilinear): its detail
//   normals (ratio kRatio: 2 m, Bridge/horizon_normal_stream) and the normals of its samples
//   (8 m), against the normal of the face at the scale of their lattice (averaged over a step of
//   it on either side along the line: the turns of the face sharper than that, at the corners of
//   its blocks, are finer than the lattice can draw): their direction kDirectionRms degrees off
//   it at most in the RMS, and the part of their direction and of their tilt repeating with a
//   wavelength of one to four steps of their lattice (the rows of triangles) kDetailPeriodic and
//   kSamplePeriodic degrees at most. A face that plunges is read where its ramp stands, a pitch
//   from the sea (coast_faces::seenOf).
// - The normals of the tiles of LOD 1 to 3, as the material of the tiles draws them (Scripts/
//   create_temporary_terrain_material.py, NORMAL_HLSL: from SmoothNearM to SmoothFarM meters
//   they lean toward the detail normals of the level 0, kLean of the way, where both agree): at the
//   distances kDistances the tiles of that LOD are drawn at, the part of their direction repeating
//   with a wavelength of one to four cells kTilePeriodic degrees at most.
// - The samples of the level 0 on the ramp of a face are bare rock (no rock and grass by turns).
// - The sea the material of the horizon paints up the wall of a face that plunges (the ground
//   interpolated between a sea sample and the land sample next to it crosses the sea level
//   there): kSeaPainted meters at most; the rest is the mask of the sea of that material (agent
//   40: only where the surface shown is at the sea level).
// All of them failed on c051ba7: the direction of the detail normals of the notched cliff 43
// degrees off the face, 10 of it repeating at the lattice.
#include "coast_render_support.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_normals.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using coast_rock::Place;
using namespace coast_render;

constexpr double kHalf = 60.0;
constexpr double kDirectionRms = 10.0;
constexpr double kDetailPeriodic = 2.5;
constexpr double kSamplePeriodic = 3.5;
constexpr double kTilePeriodic = 3.5;
constexpr double kSeaPainted = 2.0;
constexpr double kNearM = 60.0; // SmoothNearM, SmoothFarM, SmoothNormal of the material
constexpr double kFarM = 220.0;
constexpr double kLean = 0.8;

struct FacePlace {
    Place place;
    bool plunges;
};
constexpr std::array<FacePlace, 4> kPlaces{{{coast_rock::kCliff, false},
                                            {coast_rock::kRocky, false},
                                            {coast_rock::kCalanque, true},
                                            {coast_rock::kFjord, true}}};

// Where the line of a face is read on the horizon: on it, or a pitch from the sea for a face
// that plunges (its ramp rises from the shore).
Point readAt(const Point& p, bool plunges) {
    Point q = p;
    const double inset = plunges ? kPitch : 0.0;
    q.x -= p.nx * inset;
    q.z -= p.nz * inset;
    return q;
}

} // namespace

namespace {

struct Angles {
    std::vector<double> s, direction, tilt;
};

void testHorizonNormals(const FacePlace& f, const std::vector<Point>& line, const Level0& l) {
    std::array<Angles, 2> a; // detail, samples
    for (std::size_t i = 0; i < line.size(); ++i) {
        const Point& p = line[i];
        const Point q = readAt(p, f.plunges);
        for (int fine = 0; fine < 2; ++fine) {
            const auto [gx, gz] = l.slopeAt(q.x, q.z, fine == 0);
            const double lattice = fine == 0 ? kPitch / kRatio : kPitch;
            const auto angles = anglesOf(gx, gz, atScale(line, i, lattice));
            a[static_cast<std::size_t>(fine)].s.push_back(p.s);
            a[static_cast<std::size_t>(fine)].direction.push_back(angles[0]);
            a[static_cast<std::size_t>(fine)].tilt.push_back(angles[1]);
        }
    }
    for (int fine = 0; fine < 2; ++fine) {
        const Angles& e = a[static_cast<std::size_t>(fine)];
        const double lattice = fine == 0 ? kPitch / kRatio : kPitch;
        const double periodic = std::max(periodicOf(e.s, e.direction, lattice, 4.0 * lattice),
                                         periodicOf(e.s, e.tilt, lattice, 4.0 * lattice));
        std::printf("  the %s normals of the horizon: direction %.1f degrees off the face (RMS), "
                    "%.1f degrees repeating at %.0f to %.0f m\n",
                    fine == 0 ? "detail" : "sample", rmsOf(e.direction), periodic, lattice,
                    4.0 * lattice);
        TEST_CHECK(rmsOf(e.direction) <= kDirectionRms);
        TEST_CHECK(periodic <= (fine == 0 ? kDetailPeriodic : kSamplePeriodic));
    }
}

// The point of the line nearest to (x, z), and the signed distance across it (toward the air).
const Point& nearestOf(const std::vector<Point>& line, double x, double z, double& across,
                       double& off) {
    std::size_t best = 0;
    double least = 1e18;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const double d = std::hypot(x - line[i].x, z - line[i].z);
        if (d < least) {
            least = d;
            best = i;
        }
    }
    const Point& p = line[best];
    across = (x - p.x) * p.nx + (z - p.z) * p.nz;
    off = std::abs(-(x - p.x) * p.nz + (z - p.z) * p.nx);
    return p;
}

// The normal the material of the tiles draws a vertex with at dist meters: its own, leaned toward
// the detail normal of the level 0 where both agree (NORMAL_HLSL).
std::array<double, 3> drawnNormal(const terrain::Vec3f& n, const std::array<double, 2>& slope,
                                  double dist) {
    const double l = std::hypot(slope[0], slope[1], 1.0);
    const std::array<double, 3> smooth{-slope[0] / l, 1.0 / l, -slope[1] / l};
    const double agree = n.x * smooth[0] + n.y * smooth[1] + n.z * smooth[2];
    const double k = kLean * std::clamp((dist - kNearM) / (kFarM - kNearM), 0.0, 1.0) *
                     std::clamp((agree - 0.6) / 0.2, 0.0, 1.0);
    return {n.x + k * (smooth[0] - n.x), n.y + k * (smooth[1] - n.y), n.z + k * (smooth[2] - n.z)};
}

void testTileNormals(const std::vector<Point>& line, const Level0& l) {
    constexpr std::array<std::array<double, 2>, 4> kDistances{
        {{1, 96.0}, {2, 192.0}, {3, 300.0}, {3, 500.0}}};
    for (const auto& [lodValue, dist] : kDistances) {
        const int lod = static_cast<int>(lodValue);
        const double cell = terrain::tileCellMeters(lod);
        coast_rock::Tiles tiles(coast_planet::tiles(), lod);
        std::vector<std::pair<std::int64_t, std::int64_t>> seen;
        Angles e;
        for (std::size_t i = 0; i < line.size(); i += 8) {
            const terrain::TileData& t = tiles.at(line[i].x, line[i].z);
            const std::pair<std::int64_t, std::int64_t> key{std::llround(t.originX),
                                                            std::llround(t.originZ)};
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
                continue;
            }
            seen.push_back(key);
            for (std::size_t v = 0; v < t.mesh.vertices.size(); ++v) {
                const double x = t.originX + static_cast<double>(t.mesh.vertices[v].x);
                const double y = t.originY + static_cast<double>(t.mesh.vertices[v].y);
                const double z = t.originZ + static_cast<double>(t.mesh.vertices[v].z);
                double across = 0.0, off = 0.0;
                const Point& p = nearestOf(line, x, z, across, off);
                if (std::abs(across) > cell || off > kStep || y < p.foot + 3.0 ||
                    y > p.top - 3.0 || std::abs(t.mesh.normals[v].y) > 0.7f) {
                    continue;
                }
                const auto n = drawnNormal(t.mesh.normals[v], l.slopeAt(x, z, true), dist);
                e.s.push_back(p.s);
                e.direction.push_back(
                    std::atan2(-n[0] * p.nz + n[2] * p.nx, n[0] * p.nx + n[2] * p.nz) * kDegrees);
            }
        }
        const double periodic = periodicOf(e.s, e.direction, cell, 4.0 * cell);
        std::printf("  LOD %d at %.0f m: %zu vertices on the face, direction %.1f degrees off it "
                    "(RMS), %.1f degrees repeating at %.0f to %.0f m\n",
                    lod, dist, e.s.size(), rmsOf(e.direction), periodic, cell, 4.0 * cell);
        TEST_CHECK(e.s.size() >= 20 && periodic <= kTilePeriodic);
    }
}

// The samples of the level 0 on the ramp of the face, bare rock; and the sea painted up the wall
// of a face that plunges.
void testMaterialsAndSea(const FacePlace& f, const std::vector<Point>& line, const Level0& l) {
    std::size_t onRamp = 0, notRock = 0;
    double painted = 0.0;
    const std::int64_t width = l.rect.width();
    const auto cellAt = [&](std::int64_t i, std::int64_t k) -> const terrain::HorizonCell& {
        return l.cells[static_cast<std::size_t>((k - l.rect.z0) * width + (i - l.rect.x0))];
    };
    const auto isSea = [](const terrain::HorizonCell& c) {
        return (c.flags & terrain::kHorizonSea) != 0 && c.height < 0.0f;
    };
    for (std::int64_t k = l.rect.z0 + 1; k + 1 < l.rect.z1; ++k) {
        for (std::int64_t i = l.rect.x0 + 1; i + 1 < l.rect.x1; ++i) {
            const terrain::HorizonCell& c = cellAt(i, k);
            double across = 0.0, off = 0.0;
            (void)nearestOf(line, terrain::samplePosition(i, 8), terrain::samplePosition(k, 8),
                            across, off);
            if (off > kStep || isSea(c)) {
                if (f.plunges && isSea(c) && off <= kStep && std::abs(across) < 2.0 * kPitch) {
                    for (const auto& [di, dk] : std::array<std::array<int, 2>, 4>{
                             {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                        const terrain::HorizonCell& n = cellAt(i + di, k + dk);
                        const double h = n.height;
                        const double g = c.height;
                        painted = !isSea(n) && h > 0.0 ? std::max(painted, h * -g / (h - g))
                                                       : painted;
                    }
                }
                continue;
            }
            const bool ramp = f.plunges ? across < 0.0 && across > -2.0 * kPitch
                                        : std::abs(across) < kPitch;
            onRamp += ramp ? 1 : 0;
            notRock += ramp && c.material != terrain::SurfaceMaterial::Rock ? 1 : 0;
        }
    }
    std::printf("  the samples of the level 0 on the ramp: %zu, %zu of them not rock; the sea "
                "painted up the wall %.1f m at most\n",
                onRamp, notRock, painted);
    TEST_CHECK(onRamp >= 5 && notRock == 0);
    TEST_CHECK(painted <= kSeaPainted);
}

} // namespace

int main() {
    for (const FacePlace& f : kPlaces) {
        const std::vector<Point> line = lineOf(f.place, kHalf);
        std::printf("%s: its line traced over %.0f m\n", f.place.name,
                    line.empty() ? 0.0 : line.back().s);
        TEST_CHECK(static_cast<double>(line.size()) * kStep >= kHalf);
        if (line.size() < 8) {
            continue;
        }
        const Level0 l = level0Around(line);
        testHorizonNormals(f, line, l);
        testTileNormals(line, l);
        testMaterialsAndSea(f, line, l);
    }
    return test::result();
}
