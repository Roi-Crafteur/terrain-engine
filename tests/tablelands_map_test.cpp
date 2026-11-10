// The provinces of the plateaus and of the karst on the default world (agent 46, tablelands.hpp),
// switched on (off by default: then none is built):
// the same bits whatever the number of threads, for the pre-climate too; the same at every image of
// a point; all 0 where mayHold is false and at sea; their shares of the land within their bounds,
// each style there (mesas of the dry lands, plateaus of the humid ones, karst, tower karst); the
// flags of the geography take each of them out alone; out of the coasts.
#include "tablelands_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/pre_climate.hpp"
#include "terrain/generation/tablelands.hpp"
#include "terrain/generation/noise.hpp"

#include "tableland_forms.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

using terrain::GeographySample;
using terrain::Heightfield;
using terrain::TablelandPoint;
using terrain::Tablelands;

// The shares of the land, from a lattice of points kStep meters apart, within these bounds.
constexpr double kStep = 32'768.0;
constexpr double kTablelandLeast = 0.02;
constexpr double kTablelandMost = 0.06;
constexpr double kKarstLeast = 0.01;
constexpr double kKarstMost = 0.04;
// Each style holds at least this share of the land where its province is full.
constexpr double kStyleLeast = 0.001;
// A full province, and a style that rules.
constexpr float kFull = 0.5f;
// Nothing within this distance of the coast of the geography (Tablelands: kCoastNear).
constexpr double kCoastNear = 60'000.0;

const Heightfield& field() {
    return tablelands_test::field();
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void testThreads() {
    const Heightfield& f = field();
    const terrain::Tablelands& shared = *f.tablelands();
    const auto start = std::chrono::steady_clock::now();
    const auto one = std::make_shared<const terrain::PreClimate>(*f.geography(),
                                                                terrain::PlanetClimateParams{}, 1);
    const double climateSeconds = secondsSince(start);
    const auto four = std::make_shared<const terrain::PreClimate>(*f.geography(),
                                                                 terrain::PlanetClimateParams{}, 4);
    std::printf("pre-climate: %d x %d cells of %.0f m, %.2f s on one thread, %zu bytes\n",
                one->grid().nx, one->grid().nz, one->grid().cellSize, climateSeconds,
                one->memoryBytes());
    TEST_CHECK(one->checksum() == four->checksum());
    TEST_CHECK(one->checksum() == shared.climate().checksum());
    // The provinces from the same seeds on 1 and 4 threads (the seeds of the Heightfield's).
    const auto timed = std::chrono::steady_clock::now();
    const terrain::Tablelands rebuilt(shared.seeds(), shared.sharedGeography(),
                                      shared.sharedClimate(), 1);
    std::printf("provinces: %.2f s on one thread, %zu bytes\n", secondsSince(timed),
                rebuilt.memoryBytes());
    const terrain::Tablelands again(shared.seeds(), shared.sharedGeography(),
                                    shared.sharedClimate(), 4);
    TEST_CHECK(rebuilt.checksum() == shared.checksum());
    TEST_CHECK(again.checksum() == shared.checksum());
}

struct Shares {
    double land = 0.0;
    double tableland = 0.0;
    double karst = 0.0;
    double arid = 0.0;   // full plateaus of the dry lands
    double humid = 0.0;  // full plateaus of the humid lands
    double temperate = 0.0; // full karst, not tropical
    double towers = 0.0;    // full karst, tropical
    std::size_t outside = 0; // a share where mayHold is false, at sea or near the coast
    std::size_t images = 0;  // an image of a point with other bits
};

void addPoint(Shares& s, double x, double z) {
    const Heightfield& f = field();
    const GeographySample g = f.geography()->sampleAt(x, z);
    const TablelandPoint p = f.tablelandAt(x, z);
    // The provinces away from the coast, the high surfaces wherever the geography raises them.
    if ((p.any() && !f.tablelands()->mayHold(x, z)) ||
        ((p.tableland > 0.0f || p.karst > 0.0f) && !(g.coast > kCoastNear))) {
        ++s.outside;
    }
    const TablelandPoint image = f.tablelandAt(x + f.world().x, z - f.world().z);
    if (image.tableland != p.tableland || image.karst != p.karst || image.arid != p.arid) {
        ++s.images;
    }
    if (!(g.coast > 0.0)) {
        return;
    }
    s.land += 1.0;
    s.tableland += p.tableland;
    s.karst += p.karst;
    if (p.tableland > kFull) {
        (p.arid > kFull ? s.arid : s.humid) += 1.0;
    }
    if (p.karst > kFull) {
        (p.tropical > kFull ? s.towers : s.temperate) += 1.0;
    }
}

void testShares() {
    Shares s;
    const terrain::WorldSize& w = field().world();
    for (double z = -0.5 * w.z + 0.5 * kStep; z < 0.5 * w.z; z += kStep) {
        for (double x = -0.5 * w.x + 0.5 * kStep; x < 0.5 * w.x; x += kStep) {
            addPoint(s, x, z);
        }
    }
    const double t = s.tableland / s.land;
    const double k = s.karst / s.land;
    std::printf("land %.0f points: plateaus %.2f %% (full: dry %.2f %%, humid %.2f %%), karst %.2f "
                "%% (full: temperate %.2f %%, towers %.2f %%)\n",
                s.land, 100.0 * t, 100.0 * s.arid / s.land, 100.0 * s.humid / s.land, 100.0 * k,
                100.0 * s.temperate / s.land, 100.0 * s.towers / s.land);
    TEST_CHECK(t >= kTablelandLeast && t <= kTablelandMost);
    TEST_CHECK(k >= kKarstLeast && k <= kKarstMost);
    for (const double style : {s.arid, s.humid, s.temperate, s.towers}) {
        TEST_CHECK(style / s.land >= kStyleLeast);
    }
    TEST_CHECK(s.outside == 0);
    TEST_CHECK(s.images == 0);
}

// Each flag of the geography takes its province out alone, both flags all of them. Without the
// karst, the plateaus are the same bits; without the plateaus, the high surfaces go and the
// geography gives the plateaus behind the collisions their mountains back: the pre-climate and the
// karst around them change (at most kKarstMoved of its points).
constexpr double kKarstMoved = 0.05;
void testFlags() {
    terrain::HeightfieldParams noKarst = tablelands_test::on();
    noKarst.geography.karst = false;
    terrain::HeightfieldParams noPlateaus = tablelands_test::on();
    noPlateaus.geography.tablelands = false;
    const terrain::HeightfieldParams none; // the default: both off
    const Heightfield a(terrain::kDefaultWorldSeed, noKarst);
    const Heightfield b(terrain::kDefaultWorldSeed, noPlateaus);
    const Heightfield c(terrain::kDefaultWorldSeed, none);
    TEST_CHECK(c.tablelands() == nullptr);
    std::size_t plateaus = 0;
    std::size_t karst = 0;
    std::size_t wrong = 0;
    std::size_t moved = 0;
    const terrain::WorldSize& w = field().world();
    for (double z = -0.5 * w.z + 0.25 * kStep; z < 0.5 * w.z; z += 2.0 * kStep) {
        for (double x = -0.5 * w.x + 0.25 * kStep; x < 0.5 * w.x; x += 2.0 * kStep) {
            const TablelandPoint p = field().tablelandAt(x, z);
            const TablelandPoint pa = a.tablelandAt(x, z);
            const TablelandPoint pb = b.tablelandAt(x, z);
            plateaus += p.tableland > 0.0f ? 1 : 0;
            karst += p.karst > 0.0f ? 1 : 0;
            wrong += pa.karst != 0.0f || pa.tableland != p.tableland || pa.surface != p.surface;
            wrong += pb.tableland != 0.0f || pb.surface != 0.0f ? 1 : 0;
            moved += pb.karst != p.karst ? 1 : 0;
        }
    }
    std::printf("flags: %zu points of plateaus, %zu of karst, %zu wrong, %zu of karst moved without "
                "the plateaus\n",
                plateaus, karst, wrong, moved);
    TEST_CHECK(plateaus > 100 && karst > 100);
    TEST_CHECK(wrong == 0);
    TEST_CHECK(static_cast<double>(moved) <= kKarstMoved * static_cast<double>(karst));
}

// The high surfaces of the plateaus behind the collisions: some on the default world, about a
// seventh of them under their inner ranges (from the same noises as the Heightfield's).
constexpr double kInnerLeast = 0.10;
constexpr double kInnerMost = 0.20;
constexpr std::uint64_t kTablelandsLayer = 21; // heightfield.cpp: kTablelands
constexpr std::uint64_t kFormsStream = 1000;   // heightfield.cpp: kTablelandForms

void testSurfaces() {
    const terrain::WorldSize& w = field().world();
    const terrain::tableland_forms::Noises forms = terrain::tableland_forms::makeNoises(
        terrain::noise::deriveSeed(
            terrain::noise::deriveSeed(terrain::kDefaultWorldSeed, kTablelandsLayer), kFormsStream),
        w);
    double land = 0.0;
    double surfaces = 0.0;
    double inner = 0.0;
    const double step = 0.5 * kStep;
    for (double z = -0.5 * w.z + 0.5 * step; z < 0.5 * w.z; z += step) {
        for (double x = -0.5 * w.x + 0.5 * step; x < 0.5 * w.x; x += step) {
            const GeographySample g = field().geography()->sampleAt(x, z);
            if (!(g.coast > 0.0)) {
                continue;
            }
            land += 1.0;
            const TablelandPoint p = field().tablelandAt(x, z);
            if (!(p.surface > kFull)) {
                continue;
            }
            surfaces += 1.0;
            const float ranges = terrain::tableland_forms::innerRanges(forms, x, z, p.surface);
            inner += ranges > 0.12f ? 1.0 : 0.0;
        }
    }
    std::printf("high surfaces: %.2f %% of the land, %.1f %% of them under inner ranges\n",
                100.0 * surfaces / land, 100.0 * inner / surfaces);
    TEST_CHECK(surfaces > 100.0);
    TEST_CHECK(inner / surfaces >= kInnerLeast && inner / surfaces <= kInnerMost);
}

} // namespace

int main() {
    testThreads();
    testShares();
    testFlags();
    testSurfaces();
    return test::result();
}
