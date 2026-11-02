// Cost of the surface blend of the far horizon (HorizonCell::blend, agent 29), printed only, never
// asserted: the time of the samples of each level around two places (the mountains of the
// legacy relief and the valley of the K2), on one thread, the best of several runs, with the
// crests left to the detail (crestRatio 0: the epochs of the GPU horizon, deferCrests); and the
// memory of a sample. The same file builds against a checkout without the blend, for the times
// before (it only reads public headers).
#include "terrain/streaming/horizon_sampler.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr double kK2X = 416'857.0;
constexpr double kK2Z = -11'398'966.0;
constexpr int kRuns = 3;
// Rows of samples timed per level: 16 pieces of 16 rows would be the whole window of 257.
constexpr int kRows = 64;

// Seconds of the samples of kRows rows of the window of a level around (x, z), the best of kRuns.
double levelSeconds(const terrain::HorizonSampler& sampler, int level, double x, double z) {
    const terrain::HorizonParams& params = sampler.params();
    const terrain::HorizonRect window =
        terrain::windowAround(terrain::canonicalCenter(params, level, x, z), params.cellsPerSide);
    double best = 1e30;
    std::vector<terrain::HorizonCell> out;
    for (int run = 0; run < kRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        for (std::int64_t k = window.z0; k < window.z0 + kRows; k += 16) {
            const terrain::HorizonRect piece{window.x0, k, window.x1, k + 16};
            out.resize(static_cast<std::size_t>(piece.count()));
            sampler.fill(level, piece, out);
        }
        best = std::min(
            best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }
    return best * static_cast<double>(window.height()) / kRows;
}

void measure(const horizon_test::World& world, const char* name, double x, double z) {
    terrain::HorizonParams params;
    params.crestRatio = 0;
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    double fine = 0.0;
    double coarse = 0.0;
    std::printf("%s, 1 thread, every window of 257 x 257 samples (ms):", name);
    for (int level = 0; level < params.levels; ++level) {
        const double seconds = levelSeconds(sampler, level, x, z);
        (level < 4 ? fine : coarse) += seconds;
        std::printf(" L%d %.0f", level, 1e3 * seconds);
    }
    std::printf("\n%s: levels 0-3 %.0f ms, levels 4-9 %.0f ms, all %.0f ms\n", name, 1e3 * fine,
                1e3 * coarse, 1e3 * (fine + coarse));
}

} // namespace

int main() {
    std::printf("a horizon sample holds %zu bytes\n", sizeof(terrain::HorizonCell));
    const horizon_test::World world;
    measure(world, "mountains (horizon_test::kX, kZ)", horizon_test::kX, horizon_test::kZ);
    measure(world, "valley of the K2", kK2X, kK2Z);
    return test::result();
}
