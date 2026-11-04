// The characters of the ergs (agent 34, erg_characters.hpp), on the default world with a coarse
// planet: one per erg, never from the biome; the three characters among the five largest; their
// areas near their target shares; the same draw for the same seed; the weights of the characters
// at a point in [0, 1], summing to at most 1 (the rest realistic), and smooth (no edge).
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace {

using dunes_test::real;
using terrain::ErgCharacter;

void testDraw() {
    const std::vector<terrain::ErgSummary>& ergs = real().ergs.ergs();
    std::array<double, 3> area{};
    double total = 0.0;
    std::array<bool, 3> inFive{};
    for (std::size_t k = 0; k < ergs.size(); ++k) {
        const auto c = static_cast<std::size_t>(ergs[k].character);
        area[c] += ergs[k].area;
        total += ergs[k].area;
        if (k < 5) {
            inFive[c] = true;
        }
        TEST_CHECK(k == 0 || ergs[k - 1].area >= ergs[k].area);
    }
    std::printf("%zu ergs: realistic %.1f %%, big waves %.1f %%, giant waves %.1f %% of the area\n",
                ergs.size(), 100.0 * area[0] / total, 100.0 * area[1] / total,
                100.0 * area[2] / total);
    TEST_CHECK(ergs.size() >= 3);
    TEST_CHECK(inFive[0] && inFive[1] && inFive[2]);
    // The rule gives each erg after the third to the character that lags most: within the area
    // of the three largest of its target (they take one each, whatever their size).
    const terrain::ErgParams params;
    for (std::size_t c = 0; c < 3; ++c) {
        TEST_CHECK(std::abs(area[c] / total - params.characterShares[c]) <=
                   ergs[0].area / total + 1e-9);
    }
    // The same seed, the same draw.
    const terrain::Ergs again(dunes_test::kSeed, *real().planet, {}, terrain::ErgParams{});
    bool same = again.ergs().size() == ergs.size();
    for (std::size_t k = 0; same && k < ergs.size(); ++k) {
        same = again.ergs()[k].character == ergs[k].character && again.ergs()[k].area == ergs[k].area;
    }
    TEST_CHECK(same);
}

// Along long lines through the ergs, the weights of the characters stay in [0, 1], sum to at most
// 1, and move by little between points 200 m apart.
void testWeights() {
    float worst = 0.0f;
    bool bounded = true;
    std::size_t inErg = 0;
    std::array<std::size_t, 3> seen{};
    for (const terrain::ErgSummary& erg : real().ergs.ergs()) {
        float lastW = -1.0f;
        float lastG = -1.0f;
        for (int k = 0; k < 2000; ++k) {
            const double x = erg.x + 200.0 * k;
            const double z = erg.z + 73.0 * k;
            if (!real().ergs.mayHold(x, z)) {
                lastW = -1.0f;
                continue;
            }
            const terrain::ErgPoint e = real().ergs.at(x, z, 0.0f);
            if (!(e.mask > 0.0f)) {
                lastW = -1.0f;
                continue;
            }
            ++inErg;
            bounded = bounded && e.waves >= 0.0f && e.giants >= 0.0f && e.waves + e.giants <= 1.0f;
            seen[e.giants > 0.5f ? 2 : (e.waves > 0.5f ? 1 : 0)] += 1;
            if (lastW >= 0.0f) {
                worst = std::max({worst, std::abs(e.waves - lastW), std::abs(e.giants - lastG)});
            }
            lastW = e.waves;
            lastG = e.giants;
        }
    }
    std::printf("weights: %zu points in the ergs (%zu realistic, %zu big waves, %zu giant waves), "
                "steepest step %.4f per 200 m\n",
                inErg, seen[0], seen[1], seen[2], static_cast<double>(worst));
    TEST_CHECK(inErg > 1000 && bounded);
    TEST_CHECK(seen[0] > 0 && seen[1] > 0 && seen[2] > 0);
    TEST_CHECK(worst <= 0.02f);
}

} // namespace

int main() {
    testDraw();
    testWeights();
    return test::result();
}
