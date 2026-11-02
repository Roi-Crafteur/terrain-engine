// The rock of the walls repeats no regular pattern (phase 12A), and neither do its vertical
// grooves: the detector of pattern_support.hpp finds a row of grooves one every 16 m and not an
// irregular one; the vertical joints of the faces (wall_faces::jointAt) along lines across the
// walls by the K2 vary in spacing, width and depth, with no spacing repeating, and they keep off
// the lattice of the tiles (no deeper near their sides); and the light of
// the faces of the LOD 0 tiles there shows no band at the sides of the tiles (every 16 m, as the
// normals of the tiles once differed on both sides of them), which the detector would find.
#include "wall_faces.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "pattern_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using pattern_test::Grooves;

constexpr double kStep = 0.5;       // meters between the samples of a profile of grooves
constexpr double kDetected = 0.6;   // the peak of a row repeating at one spacing
constexpr double kIrregular = 0.45; // the peak a profile of the rock reaches at most
constexpr double kLeastVariation = 0.3;
// The highest wall by the K2 (wall_faces_test).
constexpr double kWallX = 416'000.0;
constexpr double kWallZ = -11'414'800.0;
constexpr double kTwoPi = 6.283185307179586;

// A profile of grooves (their middles, widths and depths, meters) over length meters.
std::vector<double> profileOf(const std::vector<std::array<double, 3>>& grooves, double length) {
    std::vector<double> out(static_cast<std::size_t>(length / kStep), 0.0);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double at = static_cast<double>(i) * kStep;
        for (const auto& [middle, width, depth] : grooves) {
            if (std::abs(at - middle) < 0.5 * width) {
                out[i] = std::max(out[i], depth * (1.0 - std::abs(at - middle) / (0.5 * width)));
            }
        }
    }
    return out;
}

// The peak of a profile of grooves at the spacings from 8 m (beyond the width of a groove, which
// matches itself) to 80 m, the swells slower than 40 m left out.
double peakOf(const std::vector<double>& profile) {
    return pattern_test::peakCorrelation(
        pattern_test::detrended(profile, static_cast<std::size_t>(40.0 / kStep)),
        static_cast<std::size_t>(8.0 / kStep), static_cast<std::size_t>(80.0 / kStep));
}

void testDetector() {
    std::vector<std::array<double, 3>> regular;
    for (double at = 10.0; at < 600.0; at += 16.0) {
        regular.push_back({at, 3.0, 1.0});
    }
    std::vector<std::array<double, 3>> irregular;
    std::uint64_t draw = 12345;
    const auto next = [&draw] {
        draw = draw * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(draw >> 11U) * 0x1.0p-53;
    };
    for (double at = 10.0; at < 600.0; at += 6.0 + 34.0 * next()) {
        irregular.push_back({at, 2.0 + 4.0 * next(), 0.3 + 2.2 * next()});
    }
    const std::vector<double> comb = profileOf(regular, 640.0);
    const std::vector<double> rock = profileOf(irregular, 640.0);
    const Grooves combGrooves = pattern_test::groovesOf(comb, kStep, 0.1);
    const Grooves rockGrooves = pattern_test::groovesOf(rock, kStep, 0.1);
    std::printf("detector: a comb every 16 m peaks at %.2f (spacings varying by %.2f), irregular "
                "grooves at %.2f (by %.2f)\n",
                peakOf(comb), pattern_test::variation(combGrooves.spacings), peakOf(rock),
                pattern_test::variation(rockGrooves.spacings));
    TEST_CHECK(peakOf(comb) > kDetected && pattern_test::variation(combGrooves.spacings) < 0.05);
    TEST_CHECK(peakOf(rock) < kIrregular &&
               pattern_test::variation(rockGrooves.spacings) > kLeastVariation);
}

// The joints along 8 lines 800 m long across the walls by the K2, every 22.5 degrees.
void testJoints() {
    const terrain::wall_faces::Grain grain = terrain::wall_faces::makeGrain(
        terrain::noise::deriveSeed(terrain::kDefaultWorldSeed, 7), terrain::WorldSize{});
    constexpr double kLength = 800.0;
    Grooves all;
    double peak = -1.0;
    for (int line = 0; line < 8; ++line) {
        const double angle = kTwoPi * line / 16.0;
        std::vector<double> depths;
        for (double at = -0.5 * kLength; at < 0.5 * kLength; at += kStep) {
            depths.push_back(terrain::wall_faces::jointAt(grain, kWallX + at * std::cos(angle),
                                                          kWallZ + at * std::sin(angle))
                                 .depth);
        }
        peak = std::max(peak, peakOf(depths));
        const Grooves g = pattern_test::groovesOf(depths, kStep, 0.1);
        all.widths.insert(all.widths.end(), g.widths.begin(), g.widths.end());
        all.depths.insert(all.depths.end(), g.depths.begin(), g.depths.end());
        all.spacings.insert(all.spacings.end(), g.spacings.begin(), g.spacings.end());
    }
    const double spacings = pattern_test::variation(all.spacings);
    const double widths = pattern_test::variation(all.widths);
    const double depths = pattern_test::variation(all.depths);
    const auto mean = [](const std::vector<double>& v) {
        double sum = 0.0;
        for (const double x : v) {
            sum += x;
        }
        return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
    };
    std::printf("joints along 8 lines of 800 m by the K2: %zu grooves, spaced %.0f m (varying by "
                "%.2f), %.1f m wide (by %.2f), %.1f m deep (by %.2f); the highest peak %.2f\n",
                all.widths.size(), mean(all.spacings), spacings, mean(all.widths), widths,
                mean(all.depths), depths, peak);
    TEST_CHECK(all.widths.size() > 100);
    TEST_CHECK(spacings > kLeastVariation && widths > 0.25 && depths > 0.25);
    TEST_CHECK(peak < kIrregular);
}

// The depth of the joints by the position within the lattice of the tiles (x and z modulo 16 m,
// by half meters) over a square of 1 km by the K2: as deep on average near the sides of the tiles
// as anywhere (the cells of the joints are not those of the tiles); against it, a comb of grooves
// on the sides of the tiles.
double sideShare(const std::vector<double>& byPosition) {
    double sum = 0.0;
    double most = 0.0;
    for (const double v : byPosition) {
        sum += v;
        most = std::max(most, v);
    }
    return sum > 0.0 ? most * static_cast<double>(byPosition.size()) / sum : 0.0;
}

void testJointsOffTheTiles() {
    const terrain::wall_faces::Grain grain = terrain::wall_faces::makeGrain(
        terrain::noise::deriveSeed(terrain::kDefaultWorldSeed, 7), terrain::WorldSize{});
    constexpr std::size_t kBins = 32;
    std::vector<double> depth(kBins, 0.0);
    std::vector<double> comb(kBins, 0.0);
    for (double z = kWallZ - 500.0; z < kWallZ + 500.0; z += 5.0) {
        for (double x = kWallX - 500.0; x < kWallX + 500.0; x += kStep) {
            const auto bin = static_cast<std::size_t>(std::fmod(x - 16.0 * std::floor(x / 16.0), 16.0) / kStep) % kBins;
            depth[bin] += terrain::wall_faces::jointAt(grain, x, z).depth;
            comb[bin] += bin < 3 || bin > kBins - 3 ? 1.0 : 0.0;
        }
    }
    std::printf("the depth of the joints by the meter of the tiles: the deepest half meter %.2f "
                "times the mean (a comb on their sides: %.2f)\n",
                sideShare(depth), sideShare(comb));
    TEST_CHECK(sideShare(comb) > 2.0);
    TEST_CHECK(sideShare(depth) < 1.15); // 1.25 with cells of 16 m, on the tiles
}

// The light across the faces of the LOD 0 tiles around the wall (the horizontal normals of their
// steep vertices, x by meter of x, z by meter of z), and that light with dark bands 2 m wide at
// the sides of the tiles (half the range of a normal).
void testNoBandsAtTheSides() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const terrain::ChunkCoord c = terrain::worldToChunk(kWallX, kWallZ);
    constexpr int kTiles = 7;
    constexpr std::size_t kMeters = kTiles * 16;
    std::array<std::vector<double>, 2> sums{std::vector<double>(kMeters, 0.0),
                                            std::vector<double>(kMeters, 0.0)};
    std::array<std::vector<double>, 2> counts = sums;
    for (int dz = 0; dz < kTiles; ++dz) {
        for (int dx = 0; dx < kTiles; ++dx) {
            const terrain::TileData t =
                tiles.generate({{{c.x - kTiles / 2 + dx, c.z - kTiles / 2 + dz}, 0}, 0});
            for (std::size_t i = 0; i < t.mesh.vertices.size(); ++i) {
                const terrain::Vec3f& n = t.mesh.normals[i];
                if (std::abs(n.y) > 0.35f) {
                    continue;
                }
                const terrain::Vec3f& v = t.mesh.vertices[i];
                const std::array<std::size_t, 2> at{
                    static_cast<std::size_t>(dx * 16 + static_cast<int>(v.x)),
                    static_cast<std::size_t>(dz * 16 + static_cast<int>(v.z))};
                const std::array<float, 2> light{n.x, n.z};
                for (std::size_t a = 0; a < 2; ++a) {
                    sums[a][std::min(at[a], kMeters - 1)] += light[a];
                    counts[a][std::min(at[a], kMeters - 1)] += 1.0;
                }
            }
        }
    }
    double peak = -1.0;
    double banded = 2.0;
    for (std::size_t a = 0; a < 2; ++a) {
        std::vector<double> light(kMeters, std::numeric_limits<double>::quiet_NaN());
        for (std::size_t m = 0; m < kMeters; ++m) {
            if (counts[a][m] > 0.0) {
                light[m] = sums[a][m] / counts[a][m];
            }
        }
        // The light less its swells slower than 9 m (a band of a side is 2 m wide).
        const std::vector<double> fast = pattern_test::detrended(light, 9);
        peak = std::max(peak, pattern_test::peakCorrelation(fast, 15, 17));
        peak = std::max(peak, pattern_test::peakCorrelation(fast, 31, 33));
        for (std::size_t m = 0; m < kMeters; ++m) {
            light[m] -= m % 16 < 2 ? 0.5 : 0.0;
        }
        banded = std::min(banded,
                          pattern_test::peakCorrelation(pattern_test::detrended(light, 9), 15, 17));
    }
    std::printf("the light of the faces by the K2 every meter: peaks %.2f at the spacing of the "
                "tiles (16 and 32 m); with bands at their sides, %.2f\n",
                peak, banded);
    TEST_CHECK(banded > kDetected);
    TEST_CHECK(peak < kIrregular);
}

} // namespace

int main() {
    testDetector();
    testJoints();
    testJointsOffTheTiles();
    testNoBandsAtTheSides();
    return test::result();
}
