// The seas of dunes and the coasts on the path of the far horizon (agents 34 and 33), on the
// default world with its planet file (coast_planet_support.hpp):
// - Heights: along the wall of the giant waves (the place geantes_pied_mur, toward its crest), the
//   relief a level of the horizon samples (Heightfield::seenAt, the faces of the coasts as its
//   tiles draw them) is that of the tiles at the same footprint (sampleAt): the same height, share
//   of the erg and kind of its floor, at every pitch from 8 to 512 m; the wall stands there, over
//   600 m. (Merged, seenAt once left the dunes out: the horizon showed the relief under them.)
// - Channels: on the floor of an erg far from the sea, the horizon reads the share of the erg and
//   the kind of its floor (sand, gravel, crust), and no band of the rock of the coasts nor rock of
//   them; on a cliff of the coasts (the place cote_falaise_encoche), the bands of its rock and no
//   share of an erg, no kind of floor of one.
#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/rock_faces.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr double kPi = 3.141592653589793;

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

void testGiantWall() {
    const terrain::Heightfield& field = coast_planet::field();
    const double heading = 105.0 * kPi / 180.0;
    std::size_t samples = 0;
    std::size_t differ = 0;
    double highest = 0.0;
    for (double d = 500.0; d <= 12'000.0; d += 500.0) {
        const double x = -323'111.0 + d * std::sin(heading);
        const double z = 3'952'778.0 - d * std::cos(heading);
        for (const double pitch : {8.0, 32.0, 128.0, 512.0}) {
            const terrain::Heightfield level = field.forFootprint(pitch);
            const terrain::HeightSample tile = level.sampleAt(x, z);
            const terrain::HeightSample seen = level.seenAt(x, z, pitch);
            ++samples;
            differ += same(tile.height, seen.height) && same(tile.erg, seen.erg) &&
                              same(tile.ergFloor, seen.ergFloor)
                          ? 0
                          : 1;
            highest = std::max(highest, static_cast<double>(seen.height));
        }
    }
    std::printf("the wall of the giant waves: %zu samples of the levels of the horizon, %zu differ "
                "from the tiles, highest %.1f m\n",
                samples, differ, highest);
    TEST_CHECK(differ == 0);
    TEST_CHECK(highest >= 600.0);
}

// The bands of the rock at a sample of the horizon of pitch meters: as horizonBands reads them.
terrain::RockBands bandsAt(const terrain::Heightfield& level, double x, double z, double y) {
    return terrain::rockBands(level.rockAt(x, z), y);
}

bool hasBands(const terrain::RockBands& b) {
    return b.layered > 0.0f || b.notchHeight > 0.0f || b.capHeight > 0.0f;
}

void testChannels() {
    const terrain::Heightfield level = coast_planet::field().forFootprint(64.0);
    // The floor of the giant waves, around the gravel and the crust of geantes_gravier.
    std::size_t floors = 0;
    std::size_t kinds = 0;
    std::size_t coastal = 0;
    for (int j = 0; j < 40; ++j) {
        for (int i = 0; i < 40; ++i) {
            const double x = -330'000.0 + 250.0 * i;
            const double z = 3'955'000.0 + 250.0 * j;
            const terrain::HeightSample s = level.seenAt(x, z, 64.0);
            if (!(s.erg > 0.5f) || s.dunes > 10.0f) {
                continue;
            }
            ++floors;
            kinds += s.ergFloor > 0.5f ? 1 : 0;
            coastal += s.rock > 0.0f || hasBands(bandsAt(level, x, z, s.height)) ? 1 : 0;
        }
    }
    // A cliff of the coasts: the samples around the notch of cote_falaise_encoche.
    std::size_t walls = 0;
    std::size_t ergs = 0;
    for (int j = -20; j <= 20; ++j) {
        for (int i = -20; i <= 20; ++i) {
            const double x = 429'778.8 + 8.0 * i;
            const double z = -855'913.3 + 8.0 * j;
            const terrain::HeightSample s = level.seenAt(x, z, 8.0);
            if (!hasBands(bandsAt(level, x, z, s.height))) {
                continue;
            }
            ++walls;
            ergs += s.erg > 0.0f || s.ergFloor > 0.0f ? 1 : 0;
        }
    }
    std::printf("channels of the horizon: %zu samples of the floor of an erg (%zu gravel or crust), "
                "%zu of them with the rock or the bands of the coasts; %zu samples of a cliff with "
                "its bands, %zu of them with a share or a floor of an erg\n",
                floors, kinds, coastal, walls, ergs);
    TEST_CHECK(floors >= 100 && kinds >= 10 && coastal == 0);
    TEST_CHECK(walls >= 10 && ergs == 0);
}

} // namespace

int main() {
    testGiantWall();
    testChannels();
    return test::result();
}
