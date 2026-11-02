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
// - The walls of its inlets (fjords, rias, calanques, coves): the same.
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
#include <optional>
#include <vector>

namespace {

using terrain::CoastKind;
using terrain::Heightfield;

using coast_teeth::Errors;
using coast_teeth::Gradient;
using coast_teeth::syntheticErrors;
using coast_teeth::Teeth;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// A face is checked at the LODs whose cells are at most 1 / kLeastCells of its height.
constexpr double kLeastCells = 2.0;

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

// The tiles of the relief alone (no rock in three dimensions): the faces in two dimensions, as the
// tiles draw them where their rock is not drawn (the LOD too coarse for a face, the faces too low)
// and as the horizon samples them; the faces in three dimensions of the coasts have their own
// checks (coast_rock_test).
const terrain::TerrainTiles& tiles() {
    static const terrain::TerrainTiles t(kSeed, {}, {}, std::nullopt, {}, nullptr, false);
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
    constexpr std::size_t kSteps = 150; // every 4 m up to 600 m from the shore
    std::array<double, kSteps + 1> h{};
    for (std::size_t i = 0; i <= kSteps; ++i) {
        const double d = 4.0 * static_cast<double>(i);
        h[i] = field().heightAt(p.x + d * ux, p.z + d * uz);
    }
    std::size_t steepest = 1;
    for (std::size_t i = 1; i < kSteps; ++i) {
        steepest = h[i + 1] - h[i - 1] > h[steepest + 1] - h[steepest - 1] ? i : steepest;
    }
    const std::size_t low = steepest > 50 ? steepest - 50 : 0;
    const std::size_t high = std::min(kSteps, steepest + 50);
    const auto at = [&h](std::size_t i) { return h.begin() + static_cast<std::ptrdiff_t>(i); };
    p.height = *std::max_element(at(steepest), at(high + 1)) -
               *std::min_element(at(low), at(steepest + 1));
    p.slope = (h[steepest + 1] - h[steepest - 1]) / 8.0;
    p.shoreX = p.x;
    p.shoreZ = p.z;
    p.x += 4.0 * static_cast<double>(steepest) * ux;
    p.z += 4.0 * static_cast<double>(steepest) * uz;
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
Errors tileErrors(double x, double z, int lod, double* span = nullptr) {
    const double c = terrain::tileCellMeters(lod);
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    const terrain::TileData data = tiles().generate({terrain::tileContaining(chunk, lod), 0});
    if (span != nullptr) {
        *span = static_cast<double>(data.maxHeight - data.minHeight);
    }
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

// The walls of the inlets of every kind (the first found 100 km around coasts of each), in the
// tiles of LOD 0 to 3 a half of their width from their site on either side along x and z: clean
// wherever the tile spans kLeastCells cells of height.
void testInletWalls() {
    constexpr std::array<std::array<double, 2>, 4> kCoasts{{{1'793'548.0, 9'821'452.0},
                                                            {-15'720'496.0, -6'266'783.0},
                                                            {23'433'101.0, -2'128'828.0},
                                                            {-26'486'937.0, 7'219'391.0}}};
    std::array<bool, terrain::kInletKindCount> found{};
    bool cleanWalls = true;
    for (const auto& [cx, cz] : kCoasts) {
        const double h = 1e5;
        for (const terrain::CoastSite& s : field().inletsIn(cx - h, cz - h, cx + h, cz + h)) {
            if (found[static_cast<std::size_t>(s.inlet)]) {
                continue;
            }
            found[static_cast<std::size_t>(s.inlet)] = true;
            std::printf("walls of the %-8s (%.0f, %.0f), %.0f m wide:",
                        terrain::inletKindName(s.inlet), s.x, s.z, s.size);
            const double r = 0.5 * static_cast<double>(s.size);
            for (int lod = 0; lod < 4; ++lod) {
                const double c = terrain::tileCellMeters(lod);
                Errors worst;
                bool checked = false;
                for (const auto& [dx, dz] : {std::array<double, 2>{r, 0.0}, {-r, 0.0}, {0.0, r},
                                             {0.0, -r}}) {
                    double span = 0.0;
                    const Errors e = tileErrors(s.x + dx, s.z + dz, lod, &span);
                    const bool tall = span >= kLeastCells * c;
                    cleanWalls = cleanWalls && (!tall || coast_teeth::clean(e));
                    checked = checked || tall;
                    worst = {std::max(worst.rms, e.rms), std::max(worst.p95, e.p95),
                             std::max(worst.periodic, e.periodic)};
                }
                std::printf(" %4.1f/%4.1f/%4.1f%s |", worst.rms, worst.p95, worst.periodic,
                            checked ? " " : "*");
            }
            std::printf("\n");
        }
    }
    TEST_CHECK(std::all_of(found.begin(), found.end(), [](bool f) { return f; }));
    TEST_CHECK(cleanWalls);
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
    testInletWalls();
    testTerracesBeforeFade();
    testAlongCoasts();
    return test::result();
}
