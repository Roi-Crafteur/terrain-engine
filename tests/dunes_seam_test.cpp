// The seams of the ergs (agent 34): the share of the dunes the tiles and the horizon blend their
// materials with (SurfaceBlend::dunes) is smooth over the ground, so that two grids of other
// pitches (the tiles of two LODs, the horizon) read the same share where they meet. Read from
// the height of the dunes (c43746b) it jumped by half within a few meters: a straight line
// across the ripples, rectangles of tint along the edges of the tiles.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {

void testSmoothShare() {
    terrain::HydrologyParams p;
    p.ergs = dunes_test::smallErgParams();
    const auto hydrology = std::make_shared<const terrain::Hydrology>(
        dunes_test::kSeed, dunes_test::small().relief, dunes_test::small().planet, p);
    const terrain::Heightfield field = dunes_test::small().relief.withHydrology(hydrology);
    const terrain::Ergs& ergs = *hydrology->ergs();
    double worst = 0.0;
    int lines = 0;
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z && lines < 6;
         z += 8'192.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x && lines < 6;
             x += 8'192.0) {
            if (!ergs.mayHold(x, z) ||
                !(ergs.at(x, z, dunes_test::small().relief.sampleAt(x, z).relief).mask > 0.0f) ||
                !(field.sampleAt(x, z).erg > 0.1f)) {
                continue;
            }
            ++lines;
            double last = -1.0;
            for (double d = 0.0; d <= 2'000.0; d += 4.0) {
                const terrain::HeightSample s = field.sampleAt(x + d, z);
                terrain::SurfaceSite site;
                site.dunes = s.dunes;
                site.erg = s.erg;
                const float share = terrain::blendSurface(terrain::ClimateSample{}, site,
                                                          terrain::BiomeParams{},
                                                          terrain::MaterialParams{},
                                                          terrain::BlendParams{}, 1.0f)
                                        .dunes;
                if (last >= 0.0) {
                    worst = std::max(worst, std::abs(static_cast<double>(share) - last));
                }
                last = static_cast<double>(share);
            }
        }
    }
    std::printf("dune share over %d lines of 2 km in the ergs: at most %.3f between samples 4 m "
                "apart\n",
                lines, worst);
    TEST_CHECK(lines >= 3);
    TEST_CHECK(worst <= 0.02);
}

} // namespace

int main() {
    testSmoothShare();
    return test::result();
}
