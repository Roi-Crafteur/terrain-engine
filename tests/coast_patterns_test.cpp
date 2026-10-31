// The patterns of the coasts (agent 33): no teeth on their faces, nothing regular along them.
// - The slope law of the faces (coast_shape::faceSlopeLimit), on synthetic faces h = H
//   smoothstep(u / w) (u across the face, 0 to 45 degrees oblique to the grid) meshed as the tiles
//   mesh them (marching cubes on y - h over cubic cells of 1 to 8 m, normals by differences of the
//   density): the error of the normals of their vertices to the true ones by slope and by height
//   of the face in cells (the table "slope -> teeth", the same at every LOD for the same height in
//   cells); at the slope of the law, clean (RMS at most kCleanRms, 95th percentile kCleanP95
//   degrees) and without teeth (the part of the error repeating at the pitch of the cells along
//   each contour of the face at most kPeriodicMost degrees) at every LOD whose cells are at most
//   half the height of the face.
// - The faces of the coasts of the default world (cliffs, calanques, rocky coasts, rias,
//   mountains) in their tiles of LOD 0 to 3: the same, against the normals of the relief at the
//   scale of the cells, wherever the face is at least two cells high, and their shores at every
//   LOD.
// - The terraces of the local relief step it before the fade of the high coasts from the shore,
//   never along the shore.
// - Along the coasts of the cliffs: the height of their top, from a few meters to the highest,
//   with no regular pattern (once its autocorrelation has fallen below 0, it never comes back
//   above kRebound).
#include "coast_shape.hpp"
#include "relief_local.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_teeth_support.hpp"
#include "coast_zone_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <numbers>
#include <vector>

namespace {

using terrain::CoastKind;
using terrain::Heightfield;

using coast_teeth::Errors;
using coast_teeth::Gradient;
using coast_teeth::Teeth;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// A face is checked at the LODs whose cells are at most 1 / kLeastCells of its height.
constexpr double kLeastCells = 2.0;

// A synthetic face H meters high, as steep as slope at most, oblique by angle (radians).
struct Synthetic {
    double height;
    double width;
    double cs;
    double sn;
    double at(double x, double z, std::array<double, 2>* gradient) const {
        const double t = std::clamp((x * cs + z * sn) / width, 0.0, 1.0);
        const double dh = t > 0.0 && t < 1.0 ? height * 6.0 * t * (1.0 - t) / width : 0.0;
        if (gradient != nullptr) {
            *gradient = {dh * cs, dh * sn};
        }
        return height * t * t * (3.0 - 2.0 * t);
    }
};

// The 2 x 2 tiles of cells c centered on (cx, cz), each level of the lattice holding the face.
void meshAround(const Synthetic& f, double c, double cx, double cz, Teeth& t) {
    const double size = (terrain::kGridSamples - 1) * c;
    const Gradient gradient = [&f](double x, double z) {
        std::array<double, 2> g{};
        static_cast<void>(f.at(x, z, &g));
        return g;
    };
    const int low = static_cast<int>(std::floor(-c / size)) - 1;
    const int high = static_cast<int>(std::floor((f.height + c) / size)) + 1;
    for (int tz = 0; tz < 2; ++tz) {
        for (int tx = 0; tx < 2; ++tx) {
            for (int level = low; level <= high; ++level) {
                const std::array<double, 3> o{cx + (tx - 1) * size, level * size,
                                              cz + (tz - 1) * size};
                terrain::DensityGrid grid(0.0f);
                for (int k = 0; k < terrain::kGridSamples; ++k) {
                    for (int j = 0; j < terrain::kGridSamples; ++j) {
                        for (int i = 0; i < terrain::kGridSamples; ++i) {
                            const double h = f.at(o[0] + i * c, o[2] + k * c, nullptr);
                            grid.set(i, j, k, static_cast<float>((o[1] + j * c - h) / c));
                        }
                    }
                }
                coast_teeth::addMesh(t, terrain::marchingCubes(grid), o, c, c, gradient, 0.1);
            }
        }
    }
}

// The worst errors of a face height (meters) high, slope steep, over 10 angles to the grid, in
// cells of c: tiles centered on its foot, its middle and its top.
Errors syntheticErrors(double height, double slope, double c) {
    Errors worst;
    for (int a = 0; a < 10; ++a) {
        const double angle = a * 5.0 * std::numbers::pi / 180.0;
        const Synthetic f{height, 1.5 * height / slope, std::cos(angle), std::sin(angle)};
        Teeth t;
        for (const double across : {0.1, 0.5, 0.9}) {
            const double u = across * f.width;
            meshAround(f, c, u * f.cs + 0.37 * c, u * f.sn + 0.61 * c, t);
        }
        const Errors e = coast_teeth::errorsOf(t, c);
        worst = {std::max(worst.rms, e.rms), std::max(worst.p95, e.p95),
                 std::max(worst.periodic, e.periodic)};
    }
    return worst;
}

// The table "slope -> teeth" by height of the face in cells (1 m cells: the same at every LOD),
// the steepest clean slope of each; a teethed face of 1.25 cells shows its period.
void testTable() {
    constexpr std::array<double, 8> kSlopes{0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0};
    std::printf("slope -> teeth: RMS / 95th percentile / periodic part of the error of the "
                "normals (degrees), by height of the face in cells\n cells |");
    for (const double s : kSlopes) {
        std::printf("      %4.2f      |", s);
    }
    std::printf(" steepest clean\n");
    for (const double cells : {1.25, 2.5, 5.0, 10.0, 20.0, 40.0}) {
        std::printf("%6.2f |", cells);
        double steepest = 0.0;
        for (const double s : kSlopes) {
            const Errors e = syntheticErrors(cells, s, 1.0);
            std::printf(" %4.1f/%4.1f/%4.1f |", e.rms, e.p95, e.periodic);
            steepest = coast_teeth::clean(e) ? s : steepest;
        }
        std::printf(" %.2f\n", steepest);
    }
    const Errors teeth = syntheticErrors(1.25, 4.0, 1.0);
    TEST_CHECK(teeth.periodic > 2.0 * coast_teeth::kPeriodicMost);
}

// At the slope of the law, clean at every LOD whose cells are at most half the height of the
// face; the LODs beyond, reported.
void testLaw() {
    bool lawful = true;
    std::printf("the law: height (m), slope, then RMS / p95 / periodic at the LOD 0 to 3 "
                "(* : cells more than half the height, reported)\n");
    for (const double h : {8.0, 10.0, 14.0, 20.0, 28.0, 40.0, 57.0, 80.0, 113.0, 160.0, 226.0}) {
        const double slope = terrain::coast_shape::faceSlopeLimit(h);
        std::printf("%6.1f %4.2f |", h, slope);
        for (int lod = 0; lod < 4; ++lod) {
            const double c = terrain::tileCellMeters(lod);
            const Errors e = syntheticErrors(h, slope, c);
            const bool checked = h >= kLeastCells * c;
            lawful = lawful && (!checked || coast_teeth::clean(e));
            std::printf(" %4.1f/%4.1f/%4.1f%s |", e.rms, e.p95, e.periodic, checked ? " " : "*");
        }
        std::printf("\n");
    }
    TEST_CHECK(lawful);
    TEST_CHECK(terrain::coast_shape::faceSlopeLimit(10.0) == 0.75);
    TEST_CHECK(terrain::coast_shape::faceSlopeLimit(1000.0) == 3.0);
}

const terrain::TerrainTiles& tiles() {
    static const terrain::TerrainTiles t(kSeed);
    return t;
}

const Heightfield& field() {
    return tiles().heightfield();
}

// The gradient of the coast signal at (x, z), meters of signal per meter.
std::array<double, 2> signalGradient(double x, double z) {
    constexpr double h = 4.0;
    return {(coast_zone::coastSignal(field(), x + h, z) -
             coast_zone::coastSignal(field(), x - h, z)) /
                (2.0 * h),
            (coast_zone::coastSignal(field(), x, z + h) -
             coast_zone::coastSignal(field(), x, z - h)) /
                (2.0 * h)};
}

// Moves (x, z) onto the shore (Newton steps on the coast signal); (ux, uz) up its gradient.
bool toShore(double& x, double& z, double& ux, double& uz) {
    for (int i = 0; i < 40; ++i) {
        const double v = coast_zone::coastSignal(field(), x, z);
        const auto [gx, gz] = signalGradient(x, z);
        const double g2 = gx * gx + gz * gz;
        if (!(g2 > 0.0)) {
            return false;
        }
        if (std::abs(v) < 1.0) {
            ux = gx / std::sqrt(g2);
            uz = gz / std::sqrt(g2);
            return true;
        }
        x -= v * gx / g2;
        z -= v * gz / g2;
    }
    return false;
}

// A face of the coasts: the steepest point of the ground up the coast signal from the shore,
// the height from the foot to the top around it (200 m on either side), and its shore.
struct Place {
    CoastKind kind;
    double x = 0.0;
    double z = 0.0;
    double height = 0.0;
    double slope = 0.0;
    double shoreX = 0.0;
    double shoreZ = 0.0;
};

bool faceOf(Place& p) {
    double ux = 0.0;
    double uz = 0.0;
    if (!toShore(p.x, p.z, ux, uz)) {
        return false;
    }
    constexpr int kSteps = 150; // every 4 m up to 600 m from the shore
    std::array<double, kSteps + 1> h{};
    for (int i = 0; i <= kSteps; ++i) {
        h[i] = field().heightAt(p.x + 4.0 * i * ux, p.z + 4.0 * i * uz);
    }
    int steepest = 1;
    for (int i = 1; i < kSteps; ++i) {
        steepest = h[i + 1] - h[i - 1] > h[steepest + 1] - h[steepest - 1] ? i : steepest;
    }
    const auto low = std::max(0, steepest - 50);
    const auto high = std::min(kSteps, steepest + 50);
    p.height = *std::max_element(h.begin() + steepest, h.begin() + high + 1) -
               *std::min_element(h.begin() + low, h.begin() + steepest + 1);
    p.slope = (h[steepest + 1] - h[steepest - 1]) / 8.0;
    p.shoreX = p.x;
    p.shoreZ = p.z;
    p.x += 4.0 * steepest * ux;
    p.z += 4.0 * steepest * uz;
    return true;
}

// Faces of each kind but the low coasts, two of the cliffs (one at least kHighCliff meters high),
// where the kind weighs at least kHeavy, near the shore.
std::vector<Place> findFaces() {
    constexpr double kHighCliff = 60.0;
    constexpr float kHeavy = 0.5f;
    std::vector<Place> wanted{{CoastKind::Cliff},    {CoastKind::Cliff}, {CoastKind::Calanque},
                              {CoastKind::Rocky},    {CoastKind::Ria},   {CoastKind::Mountain},
                              {CoastKind::Fjord}};
    std::vector<Place> found;
    coast_zone::Draws draws(0x33C0A57E1ULL);
    const terrain::WorldSize& w = field().world();
    for (int tries = 0; tries < 200'000 && !wanted.empty(); ++tries) {
        Place p{CoastKind::Low, (draws.next() - 0.5) * w.x, (draws.next() - 0.5) * w.z};
        const terrain::CoastSample c = field().coastAt(p.x, p.z);
        const auto at = std::find_if(wanted.begin(), wanted.end(),
                                     [&c](const Place& q) { return q.kind == c.kind; });
        if (!(c.base > 0.0 && c.base < 6'000.0) || at == wanted.end() ||
            c.weight(c.kind) < kHeavy) {
            continue;
        }
        p.kind = c.kind;
        const bool high = p.kind == CoastKind::Cliff && at + 1 != wanted.end() &&
                          (at + 1)->kind == CoastKind::Cliff;
        if (!faceOf(p) || (high && p.height < kHighCliff)) {
            continue;
        }
        found.push_back(p);
        wanted.erase(at);
    }
    TEST_CHECK(wanted.empty());
    return found;
}

// The errors of the normals of the tile of a LOD holding (x, z), against the normals of the relief
// at the scale of its cells.
Errors tileErrors(double x, double z, int lod) {
    const double c = terrain::tileCellMeters(lod);
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    const terrain::TileData data = tiles().generate({terrain::tileContaining(chunk, lod), 0});
    const Gradient gradient = [c](double px, double pz) {
        return std::array<double, 2>{
            (field().heightAt(px + c, pz) - field().heightAt(px - c, pz)) / (2.0 * c),
            (field().heightAt(px, pz + c) - field().heightAt(px, pz - c)) / (2.0 * c)};
    };
    Teeth t;
    coast_teeth::addMesh(t, data.mesh, {data.originX, data.originY, data.originZ}, 1.0, c,
                         gradient, 0.3);
    return coast_teeth::errorsOf(t, c);
}

// The faces of the coasts in their tiles of LOD 0 to 3: clean wherever the face is at least
// kLeastCells cells high; their shores (the water, the platform, the foot of the slope) clean at
// every LOD.
void testRealFaces(const std::vector<Place>& faces) {
    bool cleanFaces = true;
    bool cleanShores = true;
    std::printf("faces of the coasts: kind (x, z), height, steepest slope, then RMS / p95 / "
                "periodic at the LOD 0 to 3 (* : face less than %.0f cells high, reported), then "
                "the same on their shore\n",
                kLeastCells);
    for (const Place& p : faces) {
        std::printf("%-8s (%.0f, %.0f) %6.1f m %4.2f |", terrain::coastKindName(p.kind), p.x, p.z,
                    p.height, p.slope);
        for (int lod = 0; lod < 4; ++lod) {
            const Errors e = tileErrors(p.x, p.z, lod);
            const bool checked = p.height >= kLeastCells * terrain::tileCellMeters(lod);
            cleanFaces = cleanFaces && (!checked || coast_teeth::clean(e));
            std::printf(" %4.1f/%4.1f/%4.1f%s |", e.rms, e.p95, e.periodic, checked ? " " : "*");
        }
        std::printf("\n%-8s (%.0f, %.0f)   shore      |", "", p.shoreX, p.shoreZ);
        for (int lod = 0; lod < 4; ++lod) {
            const Errors e = tileErrors(p.shoreX, p.shoreZ, lod);
            cleanShores = cleanShores && coast_teeth::clean(e);
            std::printf(" %4.1f/%4.1f/%4.1f  |", e.rms, e.p95, e.periodic);
        }
        std::printf("\n");
    }
    TEST_CHECK(cleanFaces);
    TEST_CHECK(cleanShores);
}

// Along the shore from (x, z), every kStep meters: the top of the ground within kInland meters.
std::vector<double> topsAlong(double x, double z, int count) {
    constexpr double kStep = 100.0;
    constexpr double kInland = 400.0;
    std::vector<double> tops;
    double tx = 0.0;
    double tz = 0.0;
    for (int i = 0; i < count; ++i) {
        double ux = 0.0;
        double uz = 0.0;
        if (!toShore(x, z, ux, uz)) {
            break;
        }
        double top = 0.0;
        for (double d = 20.0; d <= kInland; d += 20.0) {
            top = std::max(top, static_cast<double>(field().heightAt(x + d * ux, z + d * uz)));
        }
        tops.push_back(top);
        const double sign = i > 0 && -uz * tx + ux * tz < 0.0 ? -1.0 : 1.0;
        tx = -uz * sign;
        tz = ux * sign;
        x += kStep * tx;
        z += kStep * tz;
    }
    return tops;
}

// The highest autocorrelation of v at a lag beyond the one where it first falls below 0, up to a
// quarter of its length.
double reboundOf(const std::vector<double>& v) {
    double mean = 0.0;
    for (const double a : v) {
        mean += a / static_cast<double>(v.size());
    }
    const auto at = [&](std::size_t lag) {
        double sum = 0.0;
        double var = 0.0;
        for (std::size_t i = 0; i + lag < v.size(); ++i) {
            sum += (v[i] - mean) * (v[i + lag] - mean);
        }
        for (const double a : v) {
            var += (a - mean) * (a - mean);
        }
        return var > 0.0 ? sum / var * static_cast<double>(v.size()) /
                               static_cast<double>(v.size() - lag)
                         : 0.0;
    };
    std::size_t lag = 1;
    while (lag < v.size() / 4 && at(lag) > 0.0) {
        ++lag;
    }
    double most = -1.0;
    for (; lag < v.size() / 4; ++lag) {
        most = std::max(most, at(lag));
    }
    return most;
}

// The terraces of the local relief step it before the fade of the high coasts from the shore
// (relief_local::Shape::fade): faded by f, the relief is f times the relief unfaded, its terraces
// along the hills; stepping the faded relief instead set them along the shore (the terraces
// parallel to the bay under the mountains checked in Unreal at the stop point 1).
void testTerracesBeforeFade() {
    namespace rl = terrain::relief_local;
    const rl::Noises noises = rl::makeNoises(kSeed, field().world(), true);
    coast_zone::Draws draws(0x33C0A57E3ULL);
    double most = 0.0;
    for (int i = 0; i < 2'000; ++i) {
        const double x = (draws.next() - 0.5) * 1.0e6;
        const double z = (draws.next() - 0.5) * 1.0e6;
        const auto amplitude = static_cast<float>(20.0 + 200.0 * draws.next());
        const auto fade = static_cast<float>(0.05 + 0.9 * draws.next());
        const rl::Shape full{amplitude, 0.0f, 0.0f, 1.0f, 0.3f, 1.0f};
        rl::Shape faded = full;
        faded.amplitude = fade * amplitude;
        faded.fade = fade;
        const double a = fade * rl::rolling(noises, x, z, full, nullptr);
        const double b = rl::rolling(noises, x, z, faded, nullptr);
        most = std::max(most, std::abs(a - b) / static_cast<double>(amplitude));
    }
    std::printf("terraces before the fade: the faded relief off by %.2e of the amplitude at most\n",
                most);
    TEST_CHECK(most < 1.0e-4);
}

// Along the coasts of the cliffs: tops from a few meters to the highest, without a regular
// pattern.
void testAlongCoasts() {
    constexpr double kRebound = 0.6;
    constexpr int kTraces = 4;
    coast_zone::Draws draws(0x33C0A57E2ULL);
    const terrain::WorldSize& w = field().world();
    std::vector<double> all;
    double most = -1.0;
    int traces = 0;
    while (traces < kTraces) {
        const double x = (draws.next() - 0.5) * w.x;
        const double z = (draws.next() - 0.5) * w.z;
        const terrain::CoastSample c = field().coastAt(x, z);
        if (!(c.base > 0.0 && c.base < 2'000.0) || c.kind != CoastKind::Cliff ||
            c.weight(CoastKind::Cliff) < 0.7f) {
            continue;
        }
        const std::vector<double> tops = topsAlong(x, z, 300);
        if (tops.size() < 200) {
            continue;
        }
        ++traces;
        const double rebound = reboundOf(tops);
        std::printf("cliffs from (%.0f, %.0f): %zu tops every 100 m, from %.1f to %.1f m, "
                    "autocorrelation back up to %.2f\n",
                    x, z, tops.size(), *std::min_element(tops.begin(), tops.end()),
                    *std::max_element(tops.begin(), tops.end()), rebound);
        most = std::max(most, rebound);
        all.insert(all.end(), tops.begin(), tops.end());
    }
    std::sort(all.begin(), all.end());
    const double p10 = all[all.size() / 10];
    const double p90 = all[all.size() * 9 / 10];
    std::printf("tops of the cliffs: 10th percentile %.1f m, median %.1f m, 90th %.1f m\n", p10,
                all[all.size() / 2], p90);
    TEST_CHECK(most < kRebound);
    TEST_CHECK(p90 > 3.0 * p10);
}

} // namespace

int main() {
    testTable();
    testLaw();
    const std::vector<Place> faces = findFaces();
    testRealFaces(faces);
    testTerracesBeforeFade();
    testAlongCoasts();
    return test::result();
}
