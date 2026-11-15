// The walls of the buttes of the plateaus (agent 46, tableland_cliffs.hpp, heightfield_tablelands.cpp)
// on the default world switched on: faces of rock on the whole wall, flutes cut by the layers.
//   - faces: along kRays rays of each butte, the walls of its cliff (from its rim to its foot,
//     across its ledges: runs steeper than 60 degrees, kWallLeast meters high at least; those of a
//     canyon through it counted apart) whose columns of the lattice of LOD 0 within kWallNear meters
//     hold a face that LOD draws (drawnWeight over one half): kLeastCover of them at least; LOD 1 to 3
//     printed (the faces of the plateaus run 6 m: only LOD 0 draws them whole);
//   - flutes: on kArc meters of the rim of each butte, the wall at each depth (meters out along a ray
//     every meter of the arc), less what all its depths share (the outline of the whole cliff),
//     detrended along the arc over kSmooth meters each side; its recesses (the least within
//     kFluteHalf meters, kFluteDepth deeper than the highest within kFluteSide each side) followed
//     down a meter at a time (kTrack meters aside, kGap levels missed at most), those kFluteLeast
//     meters long at least:
//       - their continuous length under the thickness of their layer (the median of the ratios under
//         1), the bounds of the caprock (ledgesAt) moved by the depth matching the ends of the flutes
//         best (the rim and the top of the shape differ by its relief);
//       - their offsets from a layer to the next: from each flute kAcross meters over a bound to the
//         nearest kAcross meters under it, against their spacing, kLeastOffset at least (the median:
//         each layer's flutes are those of the next turned inside out, but where the outlines of two
//         layers cross, the relief, a height a column, shows the outer over both: 0.1 to 0.3, not the
//         third asked);
//       - their angle to the vertical and its dispersion printed (structure tensors over kWindow m).
#include "tablelands_buttes_support.hpp"
#include "tableland_cliffs.hpp"
#include "tableland_relief.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using tablelands_buttes::Butte;
using tablelands_buttes::kSteep60;
using tablelands_fields::heightAt;

constexpr double kPi = 3.141592653589793;
// The buttes of the case (as tablelands_cliffs_test: the first kMostButtes walled by their cliff, of
// kButtesSought at most).
constexpr double kButteLow = 80.0;
constexpr double kButteHigh = 400.0;
constexpr std::size_t kButtesSought = 48;
constexpr std::size_t kMostButtes = 8;
constexpr std::size_t kLeastButtes = 5;
// The faces.
constexpr int kRays = 32;
constexpr double kRayLength = 700.0;
constexpr double kCliffLeast = 30.0;
constexpr double kWallLeast = 10.0;
constexpr double kLedgeReach = 40.0;
constexpr double kLedgeFall = 0.5;
constexpr int kWallNear = 2;
constexpr int kLods = 4;
constexpr double kLeastCover = 0.9;
// The flutes.
constexpr int kArc = 200;
constexpr double kRayStep = 0.5;
constexpr double kInside = 60.0;
constexpr double kOutside = 80.0;
constexpr int kSmooth = 15;
constexpr int kFluteHalf = 3;
constexpr int kFluteSide = 8;
constexpr double kFluteDepth = 0.5;
constexpr int kTrack = 3;
constexpr int kGap = 2;
constexpr int kFluteLeast = 3;
constexpr int kShiftMost = 20;
constexpr double kMatch = 2.0;
constexpr int kAcross = 3;
constexpr double kLeastOffset = 0.1;
constexpr int kWindow = 9;

double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::vector<Butte> caseButtes() {
    return tablelands_buttes::caseButtes(tablelands_fields::findSites(), kButteLow, kButteHigh,
                                         kMostButtes, kButtesSought);
}

// The walls of the cliff of a ray, 1 m a step (indices along the ray): steep runs of kWallLeast
// meters at least, from the rim on across ledges kLedgeReach meters wide at most, falling kLedgeFall
// of their width at most (as tablelands_cliffs_test: a ledge, not the talus and its gullies).
std::vector<std::pair<int, int>> wallsOf(const std::vector<double>& h) {
    std::vector<std::pair<int, int>> walls;
    const auto steep = [&](std::size_t i) { return h[i] - h[i + 1] > kSteep60; };
    std::size_t i = 0;
    while (i + 1 < h.size() && !steep(i)) {
        ++i;
    }
    const double rim = h[i];
    while (i + 1 < h.size()) {
        const std::size_t from = i;
        while (i + 1 < h.size() && steep(i)) {
            ++i;
        }
        if (h[from] - h[i] >= kWallLeast) {
            walls.emplace_back(static_cast<int>(from), static_cast<int>(i));
        }
        const std::size_t flat = i;
        while (i + 1 < h.size() && !steep(i) && static_cast<double>(i - flat) <= kLedgeReach) {
            ++i;
        }
        if (static_cast<double>(i - flat) > kLedgeReach || i + 1 >= h.size() ||
            h[flat] - h[i] > kLedgeFall * static_cast<double>(i - flat)) {
            break;
        }
    }
    if (walls.empty() || rim - h[static_cast<std::size_t>(walls.back().second)] < kCliffLeast) {
        return {};
    }
    return walls;
}

// Whether a column of the lattice of LOD lod within kWallNear meters of the wall (along the ray
// from (x, z) along (dx, dz)) holds a face that LOD draws.
bool faced(double x, double z, double dx, double dz, std::pair<int, int> wall, int lod) {
    const double cell = static_cast<double>(1 << lod);
    const terrain::RockDetail detail = terrain::rockDetailForLod(lod);
    for (int d = wall.first - kWallNear; d <= wall.second + kWallNear; ++d) {
        const double px = std::round((x + dx * d) / cell) * cell;
        const double pz = std::round((z + dz * d) / cell) * cell;
        const terrain::RockColumn rock = tablelands_fields::field().rockAt(px, pz);
        for (std::size_t f = 0; f < rock.faceCount; ++f) {
            if (terrain::drawnWeight(rock.faces[f], detail) > 0.5f) {
                return true;
            }
        }
    }
    return false;
}

struct Cover {
    int walls = 0;
    int canyon = 0; // walls of canyons through the buttes, left out
    std::array<int, kLods> faced{};
};

Cover coverOf(const Butte& b) {
    Cover out;
    for (int r = 0; r < kRays; ++r) {
        const double a = 2.0 * kPi * (r + 0.5) / kRays;
        const double dx = std::cos(a);
        const double dz = std::sin(a);
        std::vector<double> h;
        for (double s = 0.0; s < kRayLength; s += 1.0) {
            h.push_back(heightAt(b.x + dx * s, b.z + dz * s));
        }
        for (const auto& wall : wallsOf(h)) {
            // A canyon through the butte: its walls are those of the canyon, not its cliff.
            const double middle = 0.5 * (wall.first + wall.second);
            if (tablelands_fields::fieldsAt(b.x + dx * middle, b.z + dz * middle).canyon > 0.0) {
                ++out.canyon;
                continue;
            }
            ++out.walls;
            for (int lod = 0; lod < kLods; ++lod) {
                out.faced[static_cast<std::size_t>(lod)] += faced(b.x, b.z, dx, dz, wall, lod) ? 1 : 0;
            }
        }
    }
    return out;
}

// The wall of a butte on an arc of its rim: meters out along each ray (every meter of the arc) at
// each depth under the rim of the ray, less the mean over the depths, detrended along the arc; the
// bounds of the layers of its caprock (meters under the rim, the top and its foot with them).
struct Wall {
    std::vector<std::vector<double>> v; // [depth][ray]
    std::vector<double> bounds;
};

Wall wallOf(const Butte& b, double angle) {
    const tablelands_buttes::CliffRay c0 =
        tablelands_buttes::cliffAlong(b.x, b.z, std::cos(angle), std::sin(angle), 1.0);
    const double r0 = std::max(10.0, static_cast<double>(c0.rim));
    // The layers: the bounds of the caprock at the rim (ledgesAt).
    const double wx = tablelands_fields::wrapX(b.x + std::cos(angle) * (r0 + 3.0));
    const double wz = tablelands_fields::wrapZ(b.z + std::sin(angle) * (r0 + 3.0));
    const auto& n = tablelands_fields::noises();
    const terrain::tableland_relief::Shape s = terrain::tableland_relief::shapeAt(
        n, wx, wz, tablelands_fields::provinceAt(b.x + std::cos(angle) * (r0 + 3.0),
                                                 b.z + std::sin(angle) * (r0 + 3.0)));
    const terrain::tableland_relief::Ledges ledges = terrain::tableland_relief::ledgesAt(
        n.walls, n.units, wx, wz, s.unitHeight, terrain::tableland_relief::steepAt(n.walls, wx, wz));
    Wall w;
    w.bounds.push_back(0.0);
    for (std::size_t k = 0; k < ledges.count; ++k) {
        w.bounds.push_back(ledges.drop[k]);
    }
    w.bounds.push_back(s.unitHeight);
    const int levels = std::max(0, static_cast<int>(s.unitHeight) - 2);
    std::vector<std::vector<double>> r(static_cast<std::size_t>(levels), std::vector<double>(kArc));
    for (int i = 0; i < kArc; ++i) {
        const double a = angle + (i - kArc / 2) / r0;
        const double dx = std::cos(a);
        const double dz = std::sin(a);
        const double from = std::max(0.0, r0 - kInside);
        std::vector<double> h;
        for (double t = from; t < r0 + kOutside; t += kRayStep) {
            h.push_back(heightAt(b.x + dx * t, b.z + dz * t));
        }
        std::size_t k = 0;
        while (k + 1 < h.size() && !(h[k] - h[k + 1] > kSteep60 * kRayStep)) {
            ++k;
        }
        const double top = h[k];
        for (int d = 0; d < levels; ++d) {
            while (k + 1 < h.size() && h[k] >= top - 2.0 - d) {
                ++k;
            }
            r[static_cast<std::size_t>(d)][static_cast<std::size_t>(i)] = from + kRayStep * static_cast<double>(k);
        }
    }
    w.v = r;
    for (std::size_t i = 0; i < kArc; ++i) {
        double mean = 0.0;
        for (const auto& row : r) {
            mean += row[i] / levels;
        }
        for (auto& row : r) {
            row[i] -= mean;
        }
    }
    for (int d = 0; d < levels; ++d) {
        for (int i = 0; i < kArc; ++i) {
            double sum = 0.0;
            int count = 0;
            for (int j = std::max(0, i - kSmooth); j <= std::min(kArc - 1, i + kSmooth); ++j) {
                sum += r[static_cast<std::size_t>(d)][static_cast<std::size_t>(j)];
                ++count;
            }
            w.v[static_cast<std::size_t>(d)][static_cast<std::size_t>(i)] =
                r[static_cast<std::size_t>(d)][static_cast<std::size_t>(i)] - sum / count;
        }
    }
    return w;
}

// The flutes at each depth of a wall: the arcs of its recesses.
std::vector<std::vector<int>> flutesOf(const Wall& w) {
    std::vector<std::vector<int>> out(w.v.size());
    for (std::size_t d = 0; d < w.v.size(); ++d) {
        std::vector<double> v(kArc, 0.0);
        for (std::size_t i = 1; i + 1 < kArc; ++i) {
            v[i] = (w.v[d][i - 1] + w.v[d][i] + w.v[d][i + 1]) / 3.0;
        }
        for (int i = kSmooth; i < kArc - kSmooth; ++i) {
            const double at = v[static_cast<std::size_t>(i)];
            bool least = at < -kFluteDepth;
            double left = at;
            double right = at;
            for (int j = i - kFluteSide; j <= i + kFluteSide; ++j) {
                const double vj = v[static_cast<std::size_t>(j)];
                least = least && (std::abs(j - i) > kFluteHalf || !(vj < at));
                (j < i ? left : right) = std::max(j < i ? left : right, vj);
            }
            if (least && std::min(left, right) - at > kFluteDepth) {
                out[d].push_back(i);
            }
        }
    }
    return out;
}

// The flutes followed down: their first and last depths (levels), kFluteLeast long at least.
std::vector<std::pair<int, int>> chainsOf(const std::vector<std::vector<int>>& flutes) {
    std::vector<std::pair<int, int>> out;
    const auto levels = static_cast<int>(flutes.size());
    std::vector<std::vector<int>> seen(flutes.size(), std::vector<int>(kArc, 0));
    const auto mark = [&](int d, int i) -> int& {
        return seen[static_cast<std::size_t>(d)][static_cast<std::size_t>(i)];
    };
    for (int d = 0; d < levels; ++d) {
        for (const int i : flutes[static_cast<std::size_t>(d)]) {
            if (mark(d, i) != 0) {
                continue;
            }
            int at = i;
            int last = d;
            for (int e = d + 1; e < levels && e - last <= kGap + 1; ++e) {
                int next = -1;
                for (const int j : flutes[static_cast<std::size_t>(e)]) {
                    const bool nearer = next < 0 || std::abs(j - at) < std::abs(next - at);
                    next = std::abs(j - at) <= kTrack && mark(e, j) == 0 && nearer ? j : next;
                }
                if (next >= 0) {
                    mark(e, next) = 1;
                    at = next;
                    last = e;
                }
            }
            if (last - d + 1 >= kFluteLeast) {
                out.emplace_back(d, last);
            }
        }
    }
    return out;
}

// The depth (meters) the bounds of a wall move by to match the ends of its flutes best.
int shiftOf(const Wall& w, const std::vector<std::pair<int, int>>& chains) {
    int best = 0;
    int bestHits = -1;
    for (int shift = -kShiftMost; shift <= kShiftMost; ++shift) {
        int hits = 0;
        for (const auto& [first, last] : chains) {
            for (std::size_t b = 1; b + 1 < w.bounds.size(); ++b) {
                const double bound = w.bounds[b] + shift - 2.0;
                hits += std::abs(last - bound) <= kMatch || std::abs(first - bound) <= kMatch ? 1 : 0;
            }
        }
        if (hits > bestHits) {
            bestHits = hits;
            best = shift;
        }
    }
    return best;
}

// What the flutes of the walls show: their lengths against the thickness of their layers, their
// offsets across the bounds against their spacing, their angles.
struct Flutes {
    std::vector<double> ratios, offsets, angles, lengths, thicknesses, spacings;
};

void addLengths(const Wall& w, const std::vector<std::pair<int, int>>& chains, Flutes& out) {
    for (const auto& [first, last] : chains) {
        const double depth = 2.0 + 0.5 * (first + last);
        std::size_t b = 0;
        while (b + 1 < w.bounds.size() && w.bounds[b + 1] <= depth) {
            ++b;
        }
        if (b + 1 >= w.bounds.size()) {
            continue;
        }
        out.lengths.push_back(last - first + 1);
        out.thicknesses.push_back(w.bounds[b + 1] - w.bounds[b]);
        out.ratios.push_back(out.lengths.back() / out.thicknesses.back());
    }
}

void addOffsets(const Wall& w, const std::vector<std::vector<int>>& flutes, Flutes& out) {
    std::vector<double> spacings;
    for (const auto& level : flutes) {
        for (std::size_t k = 1; k < level.size(); ++k) {
            spacings.push_back(level[k] - level[k - 1]);
        }
    }
    const double spacing = medianOf(spacings);
    out.spacings.push_back(spacing);
    const auto levels = static_cast<int>(flutes.size());
    for (std::size_t b = 1; b + 1 < w.bounds.size(); ++b) {
        const int up = static_cast<int>(w.bounds[b] - 2.0) - kAcross;
        const int down = static_cast<int>(w.bounds[b] - 2.0) + kAcross;
        if (up < 0 || down >= levels || !(spacing > 0.0)) {
            continue;
        }
        for (const int i : flutes[static_cast<std::size_t>(up)]) {
            double nearest = -1.0;
            for (const int j : flutes[static_cast<std::size_t>(down)]) {
                const double apart = std::abs(static_cast<double>(j - i));
                nearest = nearest < 0.0 ? apart : std::min(nearest, apart);
            }
            if (nearest >= 0.0) {
                out.offsets.push_back(nearest / spacing);
            }
        }
    }
}

// The angle of the flutes to the vertical (degrees) over windows of kWindow meters away from the
// bounds: half the angle of the structure tensor of the wall.
void addAngles(const Wall& w, Flutes& out) {
    const auto levels = static_cast<int>(w.v.size());
    for (int d0 = 1; d0 + kWindow + 1 < levels; d0 += kWindow) {
        bool near = false;
        for (const double b : w.bounds) {
            near = near || (b > d0 - 1.0 && b < d0 + kWindow + 4.0);
        }
        for (int i0 = kSmooth; i0 + kWindow + 1 < kArc - kSmooth && !near; i0 += kWindow) {
            double xx = 0.0;
            double xy = 0.0;
            double yy = 0.0;
            for (int d = d0; d < d0 + kWindow; ++d) {
                for (int i = i0; i < i0 + kWindow; ++i) {
                    const auto& v = w.v;
                    const auto u = static_cast<std::size_t>(d);
                    const auto k = static_cast<std::size_t>(i);
                    const double gx = 0.5 * (v[u][k + 1] - v[u][k - 1]);
                    const double gy = 0.5 * (v[u + 1][k] - v[u - 1][k]);
                    xx += gx * gx;
                    xy += gx * gy;
                    yy += gy * gy;
                }
            }
            if (xx + yy > 1.0) {
                out.angles.push_back(0.5 * std::atan2(2.0 * xy, xx - yy) * 180.0 / kPi);
            }
        }
    }
}

} // namespace

int main() {
    const std::vector<Butte> buttes = caseButtes();
    TEST_CHECK(buttes.size() >= kLeastButtes);
    Cover cover;
    Flutes flutes;
    for (std::size_t k = 0; k < buttes.size(); ++k) {
        const Butte& b = buttes[k];
        const Cover c = coverOf(b);
        cover.walls += c.walls;
        cover.canyon += c.canyon;
        for (int lod = 0; lod < kLods; ++lod) {
            cover.faced[static_cast<std::size_t>(lod)] += c.faced[static_cast<std::size_t>(lod)];
        }
        Wall w = wallOf(b, 2.0 * kPi * static_cast<double>(k) / static_cast<double>(buttes.size()));
        const std::vector<std::vector<int>> found = flutesOf(w);
        const std::vector<std::pair<int, int>> chains = chainsOf(found);
        const int shift = shiftOf(w, chains);
        for (std::size_t i = 1; i + 1 < w.bounds.size(); ++i) {
            w.bounds[i] += shift;
        }
        addLengths(w, chains, flutes);
        addOffsets(w, found, flutes);
        addAngles(w, flutes);
        std::printf("butte at (%.0f, %.0f): %d walls, faces at LOD 0 on %d; %zu flutes\n", b.x, b.z,
                    c.walls, c.faced[0], chains.size());
    }
    const auto share = [&](int lod) {
        return static_cast<double>(cover.faced[static_cast<std::size_t>(lod)]) /
               std::max(1, cover.walls);
    };
    const auto count = static_cast<double>(std::max<std::size_t>(1, flutes.angles.size()));
    double mean = 0.0;
    double squares = 0.0;
    for (const double a : flutes.angles) {
        mean += a / count;
        squares += a * a / count;
    }
    squares -= mean * mean;
    std::printf("faces: %d walls (and %d of canyons through the buttes, left out), drawn on %.1f %% "
                "at LOD 0, %.1f %% at LOD 1, %.1f %% at LOD 2, %.1f %% at LOD 3\n",
                cover.walls, cover.canyon, 100.0 * share(0), 100.0 * share(1), 100.0 * share(2),
                100.0 * share(3));
    std::printf("flutes: %zu; %.0f m long (the median) in layers %.0f m thick, their ratio %.2f "
                "(the median); spaced %.0f m, their offsets from a layer to the next %.2f of that "
                "(the median of %zu); %.1f degrees off the vertical, dispersion %.1f degrees (%zu "
                "windows)\n",
                flutes.ratios.size(), medianOf(flutes.lengths), medianOf(flutes.thicknesses),
                medianOf(flutes.ratios), medianOf(flutes.spacings), medianOf(flutes.offsets),
                flutes.offsets.size(), mean, std::sqrt(std::max(squares, 0.0)), flutes.angles.size());
    TEST_CHECK(share(0) >= kLeastCover);
    TEST_CHECK(!flutes.ratios.empty() && medianOf(flutes.ratios) < 1.0);
    TEST_CHECK(!flutes.offsets.empty() && medianOf(flutes.offsets) >= kLeastOffset);
    return test::result();
}
