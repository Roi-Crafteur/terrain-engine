// The seas in the world (agent 51, seas.hpp) on the small world, the same code with the seas on
// and off: out of the sea every sample (every field), every tile of LOD 0 to 3 and every cell of
// the horizon keeps its bits; in the band of the coasts (agent 33) the sea keeps its floor; out of
// it the floor of the sea moves; the sea stays where it was; the planet level is the same; the
// seas built on one thread or on four are the same.

#include "seas_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/horizon_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using terrain::HeightSample;
using terrain::WaterKind;
using Place = std::pair<double, double>;

// Places spread over the small world (2,097 x 1,049 km), seas, coasts and lands among them.
std::vector<Place> places() {
    std::vector<Place> out;
    for (int k = 0; k < 16; ++k) {
        out.emplace_back(-1'000'000.0 + 131'071.0 * k, -480'000.0 + 61'333.0 * ((k * 7) % 16));
    }
    return out;
}

// Places on land at least 60 km from the coast signal, or at sea beyond 60 km, from a lattice of
// 64 km: eight of each, spread over the world.
std::vector<Place> farPlaces(bool land) {
    std::vector<Place> all;
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z;
         z += 65'536.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x;
             x += 65'536.0) {
            const double coast = seas_test::coastOf(dunes_test::small().relief.sampleAt(x, z));
            if (land ? coast > 60'000.0 : coast < -60'000.0) {
                all.emplace_back(x + 1'234.5, z + 2'345.5);
            }
        }
    }
    std::vector<Place> out;
    const std::size_t stride = std::max<std::size_t>(1, all.size() / 8);
    for (std::size_t k = stride / 2; k < all.size() && out.size() < 8; k += stride) {
        out.push_back(all[k]);
    }
    return out;
}

std::vector<HeightSample> window(const terrain::TerrainTiles& t, double x, double z, double side,
                                 double step) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (double v = -0.5 * side; v < 0.5 * side; v += step) {
        for (double u = -0.5 * side; u < 0.5 * side; u += step) {
            xs.push_back(terrain::wrapCoordinate(x + u, golden::kSmallPlanet.x));
            zs.push_back(terrain::wrapCoordinate(z + v, golden::kSmallPlanet.z));
        }
    }
    std::vector<HeightSample> out(xs.size());
    t.heightfield().samplesAt(xs, zs, out);
    return out;
}

struct Counts {
    std::size_t land = 0, landChanged = 0;
    std::size_t band = 0, bandChanged = 0;
    std::size_t open = 0, openChanged = 0;
    std::size_t waterChanged = 0;
    double deepest = 0.0, most = 0.0;
};

void count(const HeightSample& on, const HeightSample& off, Counts& c) {
    const bool equal = seas_test::sameSample(on, off);
    c.waterChanged += on.water != off.water ? 1 : 0;
    if (off.water != WaterKind::Ocean) {
        ++c.land;
        c.landChanged += equal ? 0 : 1;
    } else if (seas_test::coastOf(off) > -20'000.0) {
        ++c.band;
        c.bandChanged += equal ? 0 : 1;
    } else {
        ++c.open;
        c.openChanged += equal ? 0 : 1;
        c.deepest = std::max(c.deepest, -static_cast<double>(on.height));
        c.most = std::max(c.most, std::abs(static_cast<double>(on.height - off.height)));
    }
}

void testSamples() {
    const seas_test::Worlds& w = seas_test::worlds();
    Counts c;
    std::size_t single = 0;
    std::size_t singleDiffers = 0;
    for (const auto& [x, z] : places()) {
        const std::vector<HeightSample> on = window(w.withSeas, x, z, 40'000.0, 250.0);
        const std::vector<HeightSample> off = window(w.without, x, z, 40'000.0, 250.0);
        for (std::size_t i = 0; i < on.size(); ++i) {
            count(on[i], off[i], c);
        }
        // A sample alone is that of the batch.
        const HeightSample alone = w.withSeas.heightfield().sampleAt(
            terrain::wrapCoordinate(x - 20'000.0, golden::kSmallPlanet.x),
            terrain::wrapCoordinate(z - 20'000.0, golden::kSmallPlanet.z));
        ++single;
        singleDiffers += seas_test::sameSample(alone, on[0]) ? 0 : 1;
    }
    std::printf("samples: %zu on land (%zu changed), %zu at sea in the band of the coasts (%zu "
                "changed), %zu at sea beyond (%zu changed, deepest %.0f m, moved up to %.0f m), "
                "%zu changed their water, %zu single samples off their batch\n",
                c.land, c.landChanged, c.band, c.bandChanged, c.open, c.openChanged, c.deepest,
                c.most, c.waterChanged, singleDiffers);
    TEST_CHECK(c.land > 20'000 && c.landChanged == 0);
    TEST_CHECK(c.band > 2'000 && c.bandChanged == 0);
    TEST_CHECK(c.open > 20'000 && c.openChanged > c.open / 2);
    TEST_CHECK(c.waterChanged == 0);
    TEST_CHECK(single == places().size() && singleDiffers == 0);
}

// The tiles of the land far from the sea are the same to the bit, those of the open sea differ.
void testTiles() {
    const seas_test::Worlds& w = seas_test::worlds();
    std::size_t land = 0, landSame = 0, sea = 0, seaChanged = 0;
    for (int lod = 0; lod <= 3; ++lod) {
        for (const bool onLand : {true, false}) {
            for (const auto& [x, z] : farPlaces(onLand)) {
                const bool equal = seas_test::sameTile(seas_test::tileAt(w.withSeas, x, z, lod),
                                                       seas_test::tileAt(w.without, x, z, lod));
                land += onLand ? 1 : 0;
                landSame += onLand && equal ? 1 : 0;
                sea += onLand ? 0 : 1;
                seaChanged += !onLand && !equal ? 1 : 0;
            }
        }
    }
    std::printf("tiles: %zu on land (%zu the same), %zu at sea (%zu changed)\n", land, landSame,
                sea, seaChanged);
    TEST_CHECK(land >= 16 && landSame == land);
    TEST_CHECK(sea >= 16 && seaChanged == sea);
}

bool sameCell(const terrain::HorizonCell& a, const terrain::HorizonCell& b) {
    return seas_test::same(a.height, b.height) && seas_test::same(a.gradX, b.gradX) &&
           seas_test::same(a.gradZ, b.gradZ) && a.material == b.material && a.biome == b.biome &&
           a.flags == b.flags && seas_test::same(a.waterLevel, b.waterLevel) && a.blend == b.blend;
}

// The cells of the horizon over the land far from the sea are the same to the bit.
void testHorizon() {
    const seas_test::Worlds& w = seas_test::worlds();
    terrain::HorizonParams params;
    params.levels = 8;
    params.cellsPerSide = 64;
    params.world = golden::kSmallPlanet;
    const terrain::HorizonSampler on(w.withSeas.heightfield(), w.withSeas.ecology(), params);
    const terrain::HorizonSampler off(w.without.heightfield(), w.without.ecology(), params);
    std::size_t cells = 0, changed = 0, seaCells = 0, seaChanged = 0;
    for (int level = 0; level < 6; ++level) {
        for (const bool far : {true, false}) {
            for (const auto& [x, z] : farPlaces(far)) {
                const bool open = !far;
                const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, x, z);
                const terrain::HorizonRect rect{c.x - 4, c.z - 4, c.x + 4, c.z + 4};
                const std::vector<terrain::HorizonCell> a = on.sample(level, rect);
                const std::vector<terrain::HorizonCell> b = off.sample(level, rect);
                for (std::size_t i = 0; i < a.size(); ++i) {
                    const bool equal = sameCell(a[i], b[i]);
                    cells += far ? 1 : 0;
                    changed += far && !equal ? 1 : 0;
                    seaCells += open ? 1 : 0;
                    seaChanged += open && !equal ? 1 : 0;
                }
            }
        }
    }
    std::printf("horizon: %zu cells on land (%zu changed), %zu at sea (%zu changed)\n", cells,
                changed, seaCells, seaChanged);
    TEST_CHECK(cells > 500 && changed == 0);
    TEST_CHECK(seaCells > 500 && seaChanged > seaCells / 2);
}

// The planet level is built before the seas and without them: its checksum is that of main.
void testPlanet() {
    const std::uint64_t sum = dunes_test::small().planet->checksum();
    std::printf("small world planet level %016llx\n", static_cast<unsigned long long>(sum));
    TEST_CHECK(sum == 0xBFA4DE19A54DAD75ULL);
}

// The seas built on one thread and on four: the same fields, the same floor.
void testThreads() {
    const terrain::Heightfield& relief = dunes_test::small().relief;
    const terrain::Seas one(seas_test::kSeed, relief, dunes_test::small().planet.get(), {}, 1);
    const terrain::Seas four(seas_test::kSeed, relief, dunes_test::small().planet.get(), {}, 4);
    std::printf("seas: checksum %016llx, %zu bytes\n",
                static_cast<unsigned long long>(one.checksum()), one.memoryBytes());
    TEST_CHECK(one.checksum() == four.checksum());
    std::size_t sea = 0;
    std::size_t differ = 0;
    for (const auto& [x, z] : places()) {
        const HeightSample s = relief.sampleAt(x, z);
        HeightSample a = s;
        HeightSample b = s;
        one.apply(x, z, a, 0.0);
        four.apply(x, z, b, 0.0);
        sea += s.water == WaterKind::Ocean ? 1 : 0;
        differ += seas_test::sameSample(a, b) ? 0 : 1;
    }
    TEST_CHECK(sea >= 4 && differ == 0);
}

} // namespace

int main() {
    testPlanet();
    testThreads();
    testSamples();
    testTiles();
    testHorizon();
    return test::result();
}
