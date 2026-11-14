// The water of the volcanoes (agent 45, volcano_water.hpp). On a small world with many volcanoes
// and its planet level: every lake of a crater, a caldera or a maar stands at a single level,
// above its floor and under the lowest point of its rim (measured on the relief around it), and
// is held all around (no water beside dry ground below it, no other level beside it); no lake
// under an arid climate nor where a river cuts the rim; the pools of the hot springs and geysers
// hold water at their level, their terraces dry, the mud pots dry; points and batches give the
// same bits, a coarse footprint the same level, the details the same bits on 1 or 4 threads. On
// the default world: the land of the islands above 0 m is dry, a closed crater below 0 m never the
// sea, a caldera breached below 0 m the sea.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "bit_exact_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"
#include "volcano_on_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using terrain::GeothermalKind;
using terrain::GeothermalSite;
using terrain::Heightfield;
using terrain::HeightSample;
using terrain::VolcanoBasin;
using terrain::WaterKind;

constexpr double kPi = 3.14159265358979323846;

// The small world of the planet tests with a hundred times as many volcanoes (376).
terrain::HeightfieldParams manyVolcanoes() {
    terrain::HeightfieldParams params = volcano_test::on();
    params.volcanoes.referenceAreaKm2 = 2.2e7;
    return params;
}

struct World {
    Heightfield relief;
    std::shared_ptr<const terrain::PlanetLevel> planet;
    Heightfield field;
};

World makeWorld() {
    const Heightfield relief(terrain::kDefaultWorldSeed, manyVolcanoes(), golden::kSmallPlanet);
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(2)),
        golden::smallPlanetParams(2));
    auto hydrology =
        std::make_shared<const terrain::Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    return {relief, planet, relief.withHydrology(hydrology)};
}

std::vector<VolcanoBasin> allBasins(const Heightfield& f) {
    const terrain::WorldSize& w = f.world();
    return f.volcanoBasinsIn(-0.5 * w.x, -0.5 * w.z, 0.5 * w.x, 0.5 * w.z);
}

bool isLake(const HeightSample& s) {
    return s.water == WaterKind::Lake || s.water == WaterKind::SaltLake;
}

// A grid of (2n+1)^2 samples step meters apart around (x, z).
struct Grid {
    int n = 0;
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<HeightSample> s;
    [[nodiscard]] const HeightSample& at(int i, int j) const {
        return s[static_cast<std::size_t>((j + n) * (2 * n + 1) + i + n)];
    }
};

Grid gridAround(const Heightfield& f, double x, double z, double step, int n) {
    Grid g;
    g.n = n;
    for (int j = -n; j <= n; ++j) {
        for (int i = -n; i <= n; ++i) {
            g.xs.push_back(x + step * i);
            g.zs.push_back(z + step * j);
        }
    }
    g.s.resize(g.xs.size());
    f.samplesAt(g.xs, g.zs, g.s);
    return g;
}

// The lowest crest around a basin on the relief with its water: the highest ground of rays out
// to 1.45 of its radius (1.2 for a maar), the lowest of them.
double lowestCrest(const Heightfield& f, const VolcanoBasin& b) {
    double lowest = 1e30;
    const double outer = (b.maar ? 1.2 : 1.45) * static_cast<double>(b.radius);
    for (int k = 0; k < 96; ++k) {
        const double a = 2.0 * kPi * k / 96.0;
        std::vector<double> xs;
        std::vector<double> zs;
        for (int i = 0; i <= 60; ++i) {
            xs.push_back(b.x + outer * i / 60.0 * std::cos(a));
            zs.push_back(b.z + outer * i / 60.0 * std::sin(a));
        }
        std::vector<float> h(xs.size());
        f.heightsAt(xs, zs, h);
        lowest = std::min(lowest, static_cast<double>(*std::max_element(h.begin(), h.end())));
    }
    return lowest;
}

struct LakeCount {
    int lakes = 0;
    int cells = 0;
    int leaks = 0;  // a lake beside dry ground below it
    int levels = 0; // a lake beside water at another level
    int over = 0;   // a lake out of its floor and rim
};

void checkLake(const World& w, const VolcanoBasin& b, LakeCount& c) {
    const Grid g = gridAround(w.field, b.x, b.z, static_cast<double>(b.radius) / 30.0, 50);
    for (int j = 1 - g.n; j < g.n; ++j) {
        for (int i = 1 - g.n; i < g.n; ++i) {
            const HeightSample& s = g.at(i, j);
            if (!isLake(s) || s.waterLevel != b.level) {
                continue;
            }
            ++c.cells;
            for (const auto& [di, dj] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const HeightSample& o = g.at(i + di, j + dj);
                c.leaks += o.water == WaterKind::None && o.height < b.level ? 1 : 0;
                c.levels += o.water != WaterKind::None && o.waterLevel != b.level ? 1 : 0;
            }
        }
    }
    const double crest = lowestCrest(w.field, b);
    const bool inside = b.level > b.floor && !(b.level > b.spill) && b.level < crest;
    c.over += inside ? 0 : 1;
    ++c.lakes;
}

void testLakes(const World& w) {
    LakeCount c;
    int dry = 0;
    int arid = 0;
    int maars = 0;
    for (const VolcanoBasin& b : allBasins(w.field)) {
        const float humidity = w.planet->humidityAt(b.x, b.z);
        if (std::isnan(b.level)) {
            ++dry;
            continue;
        }
        arid += humidity < 0.2f ? 1 : 0;
        maars += b.maar ? 1 : 0;
        checkLake(w, b, c);
    }
    std::printf("%d lakes (%d of maars) over %d cells of their grids, %d dry basins; %d leaks, "
                "%d beside another level, %d out of their floor and rim, %d under an arid "
                "climate\n",
                c.lakes, maars, c.cells, dry, c.leaks, c.levels, c.over, arid);
    TEST_CHECK(c.lakes >= 10 && maars >= 3 && c.lakes - maars >= 3 && c.cells > 10000);
    TEST_CHECK(c.leaks == 0 && c.levels == 0 && c.over == 0 && arid == 0);
}

// The pools of the hot springs and geysers hold water at their level, their terraces are dry; the
// mud pots are dry.
void testSprings(const World& w) {
    const terrain::WorldSize& size = w.field.world();
    const std::vector<GeothermalSite> sites =
        w.field.geothermalIn(-0.5 * size.x, -0.5 * size.z, 0.5 * size.x, 0.5 * size.z);
    int pools = 0;
    int wet = 0;
    int terraces = 0;
    int pots = 0;
    for (const GeothermalSite& s : sites) {
        const HeightSample center = w.field.sampleAt(s.x, s.z);
        if (s.kind == GeothermalKind::HotSpring || s.kind == GeothermalKind::Geyser) {
            ++pools;
            wet += center.water == WaterKind::Lake && center.waterLevel == s.level ? 1 : 0;
            const double r = 1.2 * static_cast<double>(s.radius);
            const HeightSample rim = w.field.sampleAt(s.x + r, s.z);
            terraces += rim.water == WaterKind::None && !(rim.height < s.level) ? 1 : 0;
        } else if (s.kind == GeothermalKind::MudPot) {
            pots += center.water == WaterKind::None ? 1 : 0;
        }
    }
    std::printf("%zu geothermal sites: %d pools, %d wet at their level, %d terraces dry above "
                "them; %d mud pots dry\n",
                sites.size(), pools, wet, terraces, pots);
    TEST_CHECK(pools >= 20 && wet >= pools * 9 / 10 && terraces >= pools * 9 / 10 && pots > 0);
}

// Points and batches, the same bits; a coarse footprint, the same level where it is wet.
void testSameBits(const World& w) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (const VolcanoBasin& b : allBasins(w.field)) {
        for (int k = 0; k < 40; ++k) {
            const double r = static_cast<double>(b.radius) * (k % 10) / 8.0;
            xs.push_back(b.x + r * std::cos(0.7 * k));
            zs.push_back(b.z + r * std::sin(0.7 * k));
        }
    }
    std::vector<HeightSample> batch(xs.size());
    w.field.samplesAt(xs, zs, batch);
    const Heightfield coarse = w.field.forFootprint(64.0);
    std::vector<HeightSample> far(xs.size());
    coarse.samplesAt(xs, zs, far);
    int differ = 0;
    int levels = 0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        differ += bit_exact::same(batch[i], w.field.sampleAt(xs[i], zs[i])) ? 0 : 1;
        levels += isLake(batch[i]) && isLake(far[i]) && batch[i].waterLevel != far[i].waterLevel
                      ? 1
                      : 0;
    }
    std::printf("%zu points in the basins: %d differ between point and batch, %d lakes at "
                "another level at 64 m\n",
                xs.size(), differ, levels);
    TEST_CHECK(differ == 0 && levels == 0);
}

// The details read on 4 threads at once, the same basins and springs as on one.
void testThreads(const World& w) {
    const std::vector<VolcanoBasin> one = allBasins(w.field);
    const World fresh = makeWorld();
    const terrain::WorldSize& size = fresh.field.world();
    std::array<std::vector<VolcanoBasin>, 4> four;
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < four.size(); ++t) {
        threads.emplace_back([&, t]() {
            const double x0 = -0.5 * size.x + 0.25 * size.x * static_cast<double>(t);
            four[t] = fresh.field.volcanoBasinsIn(x0, -0.5 * size.z, x0 + 0.25 * size.x,
                                                  0.5 * size.z);
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    std::vector<VolcanoBasin> joined;
    for (const auto& part : four) {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    const auto byPlace = [](const VolcanoBasin& a, const VolcanoBasin& b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    };
    std::vector<VolcanoBasin> sorted = one;
    std::sort(sorted.begin(), sorted.end(), byPlace);
    std::sort(joined.begin(), joined.end(), byPlace);
    bool same = joined.size() == one.size();
    for (std::size_t i = 0; same && i < sorted.size(); ++i) {
        const VolcanoBasin& a = sorted[i];
        const VolcanoBasin& b = joined[i];
        same = a.x == b.x && a.z == b.z && a.floor == b.floor && a.spill == b.spill &&
               bit_exact::same(a.level, b.level) && a.water == b.water;
    }
    std::printf("%zu basins read on 4 threads: %s\n", joined.size(), same ? "the same" : "differ");
    TEST_CHECK(same);
}

// Whether the sea reaches a caldera of the default world: a ray from its floor out to twice its
// radius, every 20 m below 0 m.
bool openToSea(const Heightfield& field, double x, double z, double crater) {
    for (int k = 0; k < 360; ++k) {
        const double a = 2.0 * kPi * k / 360.0;
        std::vector<double> xs;
        std::vector<double> zs;
        for (double r = 0.3 * crater; r < 2.0 * crater; r += 20.0) {
            xs.push_back(x + r * std::cos(a));
            zs.push_back(z + r * std::sin(a));
        }
        std::vector<float> h(xs.size());
        field.heightsAt(xs, zs, h);
        if (std::all_of(h.begin(), h.end(), [](float v) { return v < 0.0f; })) {
            return true;
        }
    }
    return false;
}

// On the default world (its relief alone): the land of the islands is dry; the floor of an island
// caldera below 0 m is the sea when the sea reaches it, dry land otherwise.
void testIslands() {
    const Heightfield field(terrain::kDefaultWorldSeed, volcano_test::on());
    int land = 0;
    int sea = 0;
    int open = 0;
    int closed = 0;
    int wrong = 0;
    for (const terrain::VolcanoSite& v : field.volcanoes()->sites()) {
        if (!v.atSea || v.form == terrain::VolcanoForm::Seamount) {
            continue;
        }
        const terrain::VolcanoOffset top = terrain::volcanoSummit(v);
        const double cx = v.x + top.dx;
        const double cz = v.z + top.dz;
        for (int k = 0; k < 64; ++k) {
            const double r = static_cast<double>(v.radius) * (k % 16) / 16.0;
            const HeightSample s = field.sampleAt(cx + r * std::cos(0.9 * k), cz + r * std::sin(0.9 * k));
            land += !(s.height < 0.0f) ? 1 : 0;
            sea += !(s.height < 0.0f) && s.water == WaterKind::Ocean ? 1 : 0;
        }
        const HeightSample floor = field.sampleAt(cx, cz);
        if (v.form != terrain::VolcanoForm::IslandCaldera || !(floor.height < 0.0f)) {
            continue;
        }
        const bool reached = openToSea(field, cx, cz, static_cast<double>(v.crater));
        (reached ? open : closed) += 1;
        wrong += reached == (floor.water == WaterKind::Ocean) ? 0 : 1;
    }
    std::printf("islands: %d points of land, %d of them the sea; calderas below 0 m: %d open to "
                "the sea, %d closed, %d wrong\n",
                land, sea, open, closed, wrong);
    TEST_CHECK(land > 1000 && sea == 0 && open + closed >= 3 && wrong == 0);
}

} // namespace

int main() {
    const World w = makeWorld();
    testLakes(w);
    testSprings(w);
    testSameBits(w);
    testThreads(w);
    testIslands();
    return test::result();
}
