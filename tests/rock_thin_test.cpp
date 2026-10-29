// The features of the rock of the walls by the K2 thinner than a cell (phase 12A), on the lattice
// of LOD 0 and of LOD 1 over a square kilometer: blades of rock and slots of air one column wide
// (air on both sides of the rock of a column along x or along z, or the reverse) over kLevels
// heights of a cell at least, which the faces add (not those of the relief alone: the crest of a
// ridge). Marching Cubes draws such a feature as a sliver one cell thick whose normals turn
// against the light: a black slit or a splinter in a client. They stay within kMostPerThousand of
// the columns with faces. Measured: 1.38 (LOD 0) and 1.73 (LOD 1) per thousand (186 and 41 of
// them); 1.42 and 1.52 while the faces stood whole only at the full share of their crystalline
// cell (their folds then broke into saw teeth: rock_folds_test), 1.58 and 1.48 with the V-shaped
// joints before. Most of them come from the bare steps (without their grain, joints and overhangs
// there are as many), anchored column by column on a relief that does not always climb as their
// layer does.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using terrain::HeightSample;
using terrain::RockColumn;

// The foot of the highest wall of the K2 (rock_seams_test), and the square around it.
constexpr double kCenterX = 416'000.0;
constexpr double kCenterZ = -11'414'800.0;
constexpr double kHalf = 500.0;
constexpr std::size_t kRows = 64; // of a batch of columns
// Rock or air beyond kMargin of a cell of the surface (a sample at the surface decides nothing).
constexpr double kMargin = 0.3;
constexpr int kLevels = 3;
constexpr std::array<double, 2> kMostPerThousand = {1.45, 1.75}; // LOD 0 and LOD 1

struct Thin {
    std::size_t faced = 0;
    std::size_t blades = 0;
    std::size_t slots = 0;
};

// A batch of rows of columns, one more row on each side and one more column at each end than the
// rows it scans.
struct Batch {
    std::size_t width = 0;
    std::vector<HeightSample> samples;
    std::vector<RockColumn> rock;
};

// Whether the column one step before and after column i along the lattice hold the reverse of
// what column i holds at height y, by the rock (blade: rock between air) and by the relief alone.
struct Across {
    bool blade = false;
    bool slot = false;
    bool reliefBlade = false;
    bool reliefSlot = false;
};

Across across(const Batch& b, std::size_t i, std::size_t step, double y,
              const terrain::RockDetail& detail) {
    const double m = kMargin * static_cast<double>(detail.cell);
    const auto density = [&](std::size_t j) {
        return static_cast<double>(terrain::rockDensity(b.rock[j], y, detail));
    };
    const auto relief = [&](std::size_t j) { return y < b.samples[j].height; };
    const double c = density(i);
    const double before = density(i - step);
    const double after = density(i + step);
    return {c < -m && before > m && after > m, c > m && before < -m && after < -m,
            relief(i) && !relief(i - step) && !relief(i + step),
            !relief(i) && relief(i - step) && relief(i + step)};
}

// Counts the blades and the slots of column i of a batch, from the foot of its lowest face to the
// top of its highest.
void countColumn(const Batch& b, std::size_t i, const terrain::RockDetail& detail, Thin& out) {
    const double cell = detail.cell;
    const terrain::RockBounds bounds = terrain::rockBounds(b.rock[i]);
    int blades = 0;
    int slots = 0;
    for (double y = cell * std::floor(bounds.low / cell); y <= bounds.high + cell; y += cell) {
        const Across x = across(b, i, 1, y, detail);
        const Across z = across(b, i, b.width, y, detail);
        const bool relief = x.reliefBlade || z.reliefBlade || x.reliefSlot || z.reliefSlot;
        blades = (x.blade || z.blade) && !relief ? blades + 1 : 0;
        slots = (x.slot || z.slot) && !relief ? slots + 1 : 0;
        out.blades += blades == kLevels ? 1 : 0;
        out.slots += slots == kLevels ? 1 : 0;
    }
}

// The columns of the square on the lattice of cells of cell meters.
Thin scan(const terrain::Heightfield& field, float cell) {
    const terrain::RockDetail detail{cell};
    const auto n = static_cast<std::size_t>(2.0 * kHalf / cell) + 1;
    const double x0 = cell * std::floor((kCenterX - kHalf) / cell);
    const double z0 = cell * std::floor((kCenterZ - kHalf) / cell);
    Thin out;
    for (std::size_t first = 0; first < n; first += kRows) {
        const std::size_t rows = std::min(kRows, n - first) + 2;
        Batch b;
        b.width = n + 2;
        std::vector<double> xs;
        std::vector<double> zs;
        for (std::size_t k = 0; k < rows; ++k) {
            for (std::size_t i = 0; i < b.width; ++i) {
                xs.push_back(x0 + cell * (static_cast<double>(i) - 1.0));
                zs.push_back(z0 + cell * (static_cast<double>(first + k) - 1.0));
            }
        }
        b.samples.resize(xs.size());
        b.rock.resize(xs.size());
        field.samplesAt(xs, zs, b.samples, b.rock, terrain::kRunCells * cell);
        for (std::size_t k = 1; k + 1 < rows; ++k) {
            for (std::size_t i = 1; i + 1 < b.width; ++i) {
                const std::size_t at = k * b.width + i;
                if (b.rock[at].faceCount > 0) {
                    ++out.faced;
                    countColumn(b, at, detail, out);
                }
            }
        }
    }
    return out;
}

void testThin(const terrain::Heightfield& field, int lod) {
    const Thin t = scan(field, static_cast<float>(1 << lod));
    const double perThousand =
        1000.0 * static_cast<double>(t.blades + t.slots) / static_cast<double>(t.faced);
    std::printf("LOD %d by the K2: %zu columns with faces, %zu blades of rock and %zu slots of "
                "air one column wide over %d cells (%.2f per thousand)\n",
                lod, t.faced, t.blades, t.slots, kLevels, perThousand);
    TEST_CHECK(t.faced > (lod == 0 ? 100'000U : 20'000U));
    TEST_CHECK(perThousand <= kMostPerThousand[static_cast<std::size_t>(lod)]);
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    testThin(field, 0);
    testThin(field, 1);
    return test::result();
}
