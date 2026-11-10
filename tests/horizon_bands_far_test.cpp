// The bands of the rock beyond the tiles (agent 40, after the audit of the orchestrator: the
// ledges and the beds of the faces stopped at 512 m, the bands read only where the material of a
// sample is rock above the water, their caps never sent). On the default world with its planet
// file, at four walls (the notched cliff, the calanque and the fjord of agent 33, the wall by the
// K2), levels 0 to 3, kHalf samples on either side:
// - the bands of horizonBands are those of the rule before (rockBands of rockAt, one sample at a
//   time, where the material is rock above the water) bit for bit wherever that rule read them;
// - the samples whose column holds rock (rockAt) and get bands, before and now: never fewer, and
//   the foot of the wall of the calanque under the sea now banded;
// - their caps go to the GPU: pieceUploads ends with kCapFloats floats per sample, the cap and its
//   height of each (vertexBands), and texelRegions cuts them as the streamer does;
// - the time of the bands, before (one rockAt per sample) and now (one batch), against the time
//   of the samples themselves: printed for the report (the bands at most +10 % on the levels 0 to
//   3, the bar of the orchestrator).
#include "water_horizon_support.hpp"

#include "test_assert.hpp"

#include "terrain/streaming/horizon_bands.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

using water_horizon::params;

struct Place {
    const char* name;
    double x, z;
};

constexpr std::array<Place, 4> kPlaces{{{"the notched cliff", 429'816.0, -855'933.0},
                                        {"the calanque", 23'416'458.0, -2'119'970.0},
                                        {"the fjord", 1'822'100.0, 9'925'750.0},
                                        {"the wall by the K2", 416'017.0, -11'414'806.0}}};
constexpr std::int64_t kHalf = 24;
constexpr int kLevels = 4;

double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

bool banded(const terrain::RockBands& b) {
    return b.layered > 0.0f || b.notchHeight > 0.0f || b.capHeight > 0.0f;
}

struct Counts {
    int faces = 0, before = 0, now = 0, caps = 0;
    double sampleMs = 0.0, beforeMs = 0.0, nowMs = 0.0;
};

// One level of one place: the rule before, now, the faces of the columns, the caps sent.
Counts measure(int level, const Place& p) {
    static const terrain::Ecology ecology(terrain::kDefaultWorldSeed);
    static const terrain::HorizonSampler sampler(coast_planet::field(), ecology, params());
    const terrain::Heightfield& field = coast_planet::field();
    const int pitch = terrain::levelPitch(params(), level);
    const terrain::HorizonRect rect = water_horizon::rectAround(level, p.x, p.z, kHalf);
    Counts c;
    auto start = std::chrono::steady_clock::now();
    const std::vector<terrain::HorizonCell> cells = sampler.sample(level, rect);
    c.sampleMs = msSince(start);
    start = std::chrono::steady_clock::now();
    std::vector<terrain::RockBands> before(cells.size());
    std::size_t n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
            if (cells[n].material == terrain::SurfaceMaterial::Rock &&
                (cells[n].flags & terrain::kHorizonWater) == 0) {
                const terrain::RockColumn column = field.rockAt(terrain::samplePosition(i, pitch),
                                                                terrain::samplePosition(k, pitch));
                if (column.hasRock()) {
                    before[n] = terrain::rockBands(column, static_cast<double>(cells[n].height));
                }
            }
        }
    }
    c.beforeMs = msSince(start);
    start = std::chrono::steady_clock::now();
    const std::vector<terrain::RockBands> now =
        terrain::horizonBands(field, params(), level, rect, cells);
    c.nowMs = msSince(start);
    n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
            TEST_CHECK(!banded(before[n]) || before[n] == now[n]);
            const bool rock = field.rockAt(terrain::samplePosition(i, pitch),
                                           terrain::samplePosition(k, pitch)).hasRock();
            c.faces += rock ? 1 : 0;
            c.before += rock && banded(before[n]) ? 1 : 0;
            c.now += rock && banded(now[n]) ? 1 : 0;
            c.caps += now[n].capHeight > 0.0f ? 1 : 0;
        }
    }
    // The caps at the end of the uploads, and their regions.
    const terrain::HorizonPiece piece{level, rect, cells};
    const std::vector<float> uploads = terrain::unreal::pieceUploads(piece, true, now);
    const std::size_t capsAt = uploads.size() - cells.size() * terrain::unreal::kCapFloats;
    for (std::size_t m = 0; m < cells.size(); ++m) {
        const terrain::unreal::VertexBands b = terrain::unreal::vertexBands(now[m]);
        TEST_CHECK(uploads[capsAt + 2 * m] == b.cap && uploads[capsAt + 2 * m + 1] == b.capHeight);
    }
    const std::vector<terrain::unreal::TexelRegion> regions = terrain::unreal::texelRegions(
        level, terrain::samplesPerSide(params()), rect,
        std::span<const float>(uploads).subspan(capsAt), terrain::unreal::kCapFloats);
    std::size_t texels = 0;
    for (const auto& r : regions) {
        texels += r.data.size() / terrain::unreal::kCapFloats;
    }
    TEST_CHECK(texels == cells.size());
    return c;
}

} // namespace

int main() {
    double sampleMs = 0.0, beforeMs = 0.0, nowMs = 0.0;
    for (const Place& p : kPlaces) {
        for (int level = 0; level < kLevels; ++level) {
            const Counts c = measure(level, p);
            std::printf("%s, level %d: %d samples on rock faces, %d banded before, %d now, %d "
                        "with a cap; the samples %.1f ms, the bands %.1f ms before, %.1f ms now\n",
                        p.name, level, c.faces, c.before, c.now, c.caps, c.sampleMs, c.beforeMs,
                        c.nowMs);
            TEST_CHECK(c.now >= c.before);
            if (p.name == kPlaces[1].name && level == 0) {
                TEST_CHECK(c.now >= c.before + c.faces / 4); // its foot under the sea
            }
            sampleMs += c.sampleMs;
            beforeMs += c.beforeMs;
            nowMs += c.nowMs;
        }
    }
    std::printf("in all: the samples %.0f ms, their bands %.0f ms before (%+.1f %%), %.0f ms now "
                "(%+.1f %%)\n",
                sampleMs, beforeMs, 100.0 * beforeMs / sampleMs, nowMs, 100.0 * nowMs / sampleMs);
    return test::result();
}
