// The seas of dunes on the far horizon (agent 34, Ergs::dunesAt with a footprint): a grid as fine
// as the tiles sees the dunes as they are (the same bits); a coarse grid sees their mean, its
// silhouette right (the mean of the dunes within a tenth), and moving the grid by half a sample
// barely changes what it shows (no flicker), where the dunes sampled as points alias.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using dunes_test::real;

// Lines across full ergs of the default world (coarse planet): their start and direction.
struct Line {
    double x;
    double z;
};

std::vector<Line> ergLines() {
    std::vector<Line> out;
    const terrain::Ergs& ergs = real().ergs;
    const terrain::WorldSize w;
    for (double z = -0.5 * w.z; z < 0.5 * w.z && out.size() < 4; z += 131'072.0) {
        for (double x = -0.5 * w.x; x < 0.5 * w.x && out.size() < 4; x += 524'288.0) {
            bool full = true;
            for (double s = 0.0; s <= 40'000.0 && full; s += 4'000.0) {
                full = ergs.mayHold(x + s, z) && ergs.at(x + s, z, 0.0f).mask > 0.9f;
            }
            if (full) {
                out.push_back({x, z});
            }
        }
    }
    return out;
}

double dunesAt(double x, double z, double footprint) {
    terrain::HeightSample s;
    return static_cast<double>(real().ergs.dunesAt(
        terrain::wrapCoordinate(x, terrain::WorldSize{}.x), z, s, footprint));
}

// The height along the line of a grid of pitch p starting at offset, linear between its samples,
// at the point s.
double gridAt(const Line& l, double pitch, double offset, double footprint, double s) {
    const double u = (s - offset) / pitch;
    const double i = std::floor(u);
    const double a = dunesAt(l.x + offset + i * pitch, l.z, footprint);
    const double b = dunesAt(l.x + offset + (i + 1.0) * pitch, l.z, footprint);
    return a + (b - a) * (u - i);
}

struct Flicker {
    double rms = 0.0;  // of the difference between the two grids, meters
    double mean = 0.0; // of the grid at offset 0
};

Flicker flickerOf(const std::vector<Line>& lines, double pitch, double footprint) {
    double sum = 0.0;
    double mean = 0.0;
    std::size_t n = 0;
    for (const Line& l : lines) {
        for (double s = 2.0 * pitch; s < 36'000.0; s += 97.0) {
            const double a = gridAt(l, pitch, 0.0, footprint, s);
            const double b = gridAt(l, pitch, 0.5 * pitch, footprint, s);
            sum += (a - b) * (a - b);
            mean += a;
            ++n;
        }
    }
    return {std::sqrt(sum / static_cast<double>(n)), mean / static_cast<double>(n)};
}

double trueMean(const std::vector<Line>& lines) {
    double sum = 0.0;
    std::size_t n = 0;
    for (const Line& l : lines) {
        for (double s = 0.0; s < 36'000.0; s += 7.0) {
            sum += dunesAt(l.x + s, l.z, 0.0);
            ++n;
        }
    }
    return sum / static_cast<double>(n);
}

void testFineGridKeepsTheDunes(const std::vector<Line>& lines) {
    bool same = true;
    for (const Line& l : lines) {
        for (double s = 0.0; s < 4'000.0; s += 8.0) {
            const double point = dunesAt(l.x + s, l.z, 0.0);
            for (const double footprint : {8.0, 16.0, 64.0}) {
                same = same && std::bit_cast<std::uint64_t>(dunesAt(l.x + s, l.z, footprint)) ==
                                   std::bit_cast<std::uint64_t>(point);
            }
        }
    }
    TEST_CHECK(same);
}

void testCoarseGrids(const std::vector<Line>& lines) {
    const double truth = trueMean(lines);
    std::printf("dunes: true mean %.1f m along %zu lines of 36 km\n", truth, lines.size());
    for (const double pitch : {512.0, 1024.0, 2048.0, 4096.0}) {
        const Flicker raw = flickerOf(lines, pitch, 0.0);
        const Flicker filtered = flickerOf(lines, pitch, pitch);
        std::printf("pitch %4.0f m: shift by half a sample moves the ground by %5.1f m as points, "
                    "%5.1f m filtered; mean %5.1f m as points, %5.1f m filtered\n",
                    pitch, raw.rms, filtered.rms, raw.mean, filtered.mean);
        TEST_CHECK(filtered.rms <= 0.35 * raw.rms);
        TEST_CHECK(std::abs(filtered.mean - truth) <= 0.1 * truth + 1.0);
    }
}

} // namespace

int main() {
    const std::vector<Line> lines = ergLines();
    std::printf("%zu lines across full ergs\n", lines.size());
    TEST_CHECK(lines.size() >= 2);
    testFineGridKeepsTheDunes(lines);
    testCoarseGrids(lines);
    return test::result();
}
