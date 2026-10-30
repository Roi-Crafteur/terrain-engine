// The mountains of the default world are frozen (phase 11R bis, after step 5 bis): wherever their
// mask is above 0, every bit of the samples stays that of step 5 bis (357d3e9), whatever is done
// to the plains. One checksum per window, over the samples of its points under the mountains
// only: the K2 scene (view_k2), the highest wall near it (view_wall), the foot of the longest
// chain (view_foot, at the fixed place and with the default arguments), and points drawn over the
// whole world. A window that changes prints its new value; it must not be pasted back unless the
// mountains are meant to change.
// With their water (phase 11B), on the small world whose planet level a test may build: outside
// the corridors of the rivers, every bit of these samples stays that of the relief alone.
// The coastal relief (agent 33) shapes the mountains of its zone only, the band of the coast
// signal along the coasts (coast_zone_support.hpp): no point of the windows lies in it, and the
// points drawn and those beside the rivers are taken out of it.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "coast_zone_support.hpp"
#include "golden_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using terrain::HeightSample;

struct Window {
    const char* name;
    double x;    // center, meters
    double z;
    double step; // meters between points
    int side;    // points along each axis
    std::uint64_t expected;
};

void addSample(golden::Checksum& sum, const HeightSample& s) {
    sum.add(s.mountainMask);
    sum.add(s.relief);
    sum.add(s.base);
    sum.add(s.mountain);
    sum.add(s.detail);
    sum.add(s.height);
}

// The samples of the window where the mountain mask is above 0, how many, and how many of them lie
// in the zone of the coastal relief.
std::uint64_t windowSum(const terrain::Heightfield& field, const Window& w, int& count,
                        int& zoned) {
    golden::Checksum sum;
    count = 0;
    zoned = 0;
    std::vector<double> xs(static_cast<std::size_t>(w.side));
    std::vector<double> zs(static_cast<std::size_t>(w.side));
    std::vector<HeightSample> row(static_cast<std::size_t>(w.side));
    for (int j = 0; j < w.side; ++j) {
        for (int i = 0; i < w.side; ++i) {
            xs[static_cast<std::size_t>(i)] = w.x + w.step * (i - w.side / 2);
            zs[static_cast<std::size_t>(i)] = w.z + w.step * (j - w.side / 2);
        }
        field.samplesAt(xs, zs, row);
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (row[i].mountainMask > 0.0f) {
                addSample(sum, row[i]);
                ++count;
                zoned += coast_zone::inZone(coast_zone::coastSignal(field, xs[i], zs[i])) ? 1 : 0;
            }
        }
    }
    return sum.value();
}

// Points drawn over the whole world, those under the mountains out of the zone of the coastal
// relief.
std::uint64_t drawnSum(const terrain::Heightfield& field, int wanted, int& count) {
    golden::Checksum sum;
    count = 0;
    std::uint64_t r = 0x9E3779B97F4A7C15ULL;
    const terrain::WorldSize world{};
    while (count < wanted) {
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        const double u = static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        const double v = static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
        const double x = (u - 0.5) * world.x;
        const double z = (v - 0.5) * world.z;
        if (coast_zone::inZone(coast_zone::coastSignal(field, x, z))) {
            continue;
        }
        const HeightSample s = field.sampleAt(x, z);
        if (s.mountainMask > 0.0f) {
            addSample(sum, s);
            ++count;
        }
    }
    return sum.value();
}

bool sameBits(const HeightSample& a, const HeightSample& b) {
    const auto bits = [](const HeightSample& s) {
        return std::array<std::uint32_t, 6>{
            std::bit_cast<std::uint32_t>(s.mountainMask), std::bit_cast<std::uint32_t>(s.relief),
            std::bit_cast<std::uint32_t>(s.base), std::bit_cast<std::uint32_t>(s.mountain),
            std::bit_cast<std::uint32_t>(s.detail), std::bit_cast<std::uint32_t>(s.height)};
    };
    return bits(a) == bits(b);
}

// The mountains of the small world with their water, 16 km around the sources of 6 of its rivers
// under the mountains, every 125 m: the points of the corridors of the rivers (where the valley of
// a river lies below the relief or its floor fills it, Hydrology::riversAt) are left out; every
// other one keeps every bit of the relief alone, the lakes only laying their water over it.
void testWithWater() {
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      golden::kSmallPlanet);
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(2)),
        golden::smallPlanetParams(2));
    auto hydrology =
        std::make_shared<const terrain::Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    const terrain::Heightfield field = relief.withHydrology(hydrology);
    std::vector<std::array<double, 2>> sources;
    for (const terrain::RiverSegment& s : planet->rivers().segments) {
        const terrain::RiverVertex& v = s.vertices.front();
        if (sources.size() < 6 && relief.sampleAt(v.x, v.z).mountainMask > 0.0f) {
            sources.push_back({v.x, v.z});
        }
    }
    std::size_t mountains = 0;
    std::size_t excluded = 0;
    std::size_t lakes = 0;
    bool frozen = true;
    for (const auto& c : sources) {
        for (int j = -64; j < 64; ++j) {
            for (int i = -64; i < 64; ++i) {
                const double x = c[0] + 125.0 * i + 0.3;
                const double z = c[1] + 125.0 * j + 0.7;
                const HeightSample a = relief.sampleAt(x, z);
                if (!(a.mountainMask > 0.0f) ||
                    coast_zone::inZone(coast_zone::coastSignal(relief, x, z))) {
                    continue;
                }
                ++mountains;
                const HeightSample s = field.sampleAt(x, z);
                const terrain::RiverCarve carve =
                    hydrology->riversAt(x, z, static_cast<double>(a.height));
                if (carve.carved || carve.raise > 0.0) {
                    ++excluded;
                    continue;
                }
                frozen = frozen && sameBits(a, s);
                lakes += s.water == terrain::WaterKind::Lake ||
                                 s.water == terrain::WaterKind::SaltLake
                             ? 1u
                             : 0u;
            }
        }
    }
    TEST_CHECK(frozen && excluded > 0 && excluded < mountains);
    std::printf("with the water of the small world: %zu points under the mountains, %zu in the "
                "corridors of the rivers left out (%.2f%%), every bit of the others kept (%zu of "
                "them under lakes)\n",
                mountains, excluded, 100.0 * static_cast<double>(excluded) /
                                         static_cast<double>(std::max<std::size_t>(mountains, 1)),
                lakes);
}

} // namespace

int main() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const Window windows[] = {
        {"k2 scene, 8 km at 25 m", 416'857.0, -11'405'966.0, 25.0, 321, 0xF9B398B8FB28A571ULL},
        {"highest wall, 1 km at 4 m", 416'000.0, -11'414'800.0, 4.0, 251, 0x1D45408B42DF87EEULL},
        {"foot of the fixed chain, 24 km at 100 m", -27'744'000.0, 4'734'000.0, 100.0, 241,
         0x03E397534BA52149ULL},
        {"foot of the default chain, 24 km at 100 m", 9'630'245.0, -11'029'895.0, 100.0, 241,
         0x923AADA4496FDE08ULL},
    };
    bool same = true;
    for (const Window& w : windows) {
        int count = 0;
        int zoned = 0;
        const std::uint64_t value = windowSum(field, w, count, zoned);
        std::printf("{\"%s\", 0x%016" PRIX64 "ULL}, %d points under the mountains, %d in the "
                    "coastal zone%s\n",
                    w.name, value, count, zoned, value == w.expected ? "" : "  (differs)");
        same = same && value == w.expected && count > w.side * w.side / 4 && zoned == 0;
    }
    // Pinned on the code of main (cb64153) once the points of the coastal zone were taken out of
    // the draw (0xB6C1FF0E32307E10 with them).
    constexpr std::uint64_t kDrawn = 0x4D77F4B4ADE22B7BULL;
    int count = 0;
    const std::uint64_t drawn = drawnSum(field, 20'000, count);
    std::printf("{\"drawn points\", 0x%016" PRIX64 "ULL}%s\n", drawn,
                drawn == kDrawn ? "" : "  (differs)");
    TEST_CHECK(same);
    TEST_CHECK(drawn == kDrawn);
    testWithWater();
    return test::result();
}
