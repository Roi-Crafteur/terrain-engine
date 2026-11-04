// The places of the coasts of agent 33 (cote_* and stack_* in data/dev/places.json) on the default
// world with its planet file (the fixture coast_planet), the same code with the seas of agent 51
// on and off: around each place, every sample of a window of 1 km every 4 m, the tiles of LOD 0
// to 3 that hold it and the cells of the horizon of levels 0 to 5 around it keep their bits; a
// table of what changes, for the views the orchestrator takes in Unreal.
// seas_coast_places_test <data/dev/places.json>

#include "coast_planet_support.hpp"
#include "seas_support.hpp"
#include "test_assert.hpp"

#include "terrain/dev/places_file.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct World {
    std::shared_ptr<const terrain::Hydrology> water;
    std::unique_ptr<terrain::TerrainTiles> tiles;
};

World worldOf(bool seas) {
    const terrain::Hydrology& base = *coast_planet::hydrology();
    terrain::HydrologyParams p = base.params();
    p.seas.enabled = seas;
    World w;
    w.water = std::make_shared<const terrain::Hydrology>(
        terrain::kDefaultWorldSeed, terrain::Heightfield(terrain::kDefaultWorldSeed),
        std::shared_ptr<const terrain::PlanetLevel>(coast_planet::hydrology(), &base.planet()), p);
    w.tiles = std::make_unique<terrain::TerrainTiles>(
        terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, terrain::EcologyParams{},
        std::nullopt, terrain::WorldSize{}, w.water, true);
    return w;
}

struct Changes {
    std::size_t samples = 0, samplesChanged = 0, tiles = 0, tilesChanged = 0;
    std::size_t cells = 0, cellsChanged = 0;
};

void samplesAround(const World& on, const World& off, double x, double z, Changes& c) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (double v = -512.0; v < 512.0; v += 4.0) {
        for (double u = -512.0; u < 512.0; u += 4.0) {
            xs.push_back(x + u);
            zs.push_back(z + v);
        }
    }
    std::vector<terrain::HeightSample> a(xs.size());
    std::vector<terrain::HeightSample> b(xs.size());
    on.tiles->heightfield().samplesAt(xs, zs, a);
    off.tiles->heightfield().samplesAt(xs, zs, b);
    for (std::size_t i = 0; i < a.size(); ++i) {
        ++c.samples;
        c.samplesChanged += seas_test::sameSample(a[i], b[i]) ? 0 : 1;
    }
}

void horizonAround(const World& on, const World& off, double x, double z, Changes& c) {
    terrain::HorizonParams params;
    const terrain::HorizonSampler a(on.tiles->heightfield(), on.tiles->ecology(), params);
    const terrain::HorizonSampler b(off.tiles->heightfield(), off.tiles->ecology(), params);
    for (int level = 0; level < 6; ++level) {
        const terrain::HorizonIndex i = terrain::canonicalCenter(params, level, x, z);
        const terrain::HorizonRect rect{i.x - 4, i.z - 4, i.x + 4, i.z + 4};
        const std::vector<terrain::HorizonCell> ca = a.sample(level, rect);
        const std::vector<terrain::HorizonCell> cb = b.sample(level, rect);
        for (std::size_t k = 0; k < ca.size(); ++k) {
            ++c.cells;
            const bool same = seas_test::same(ca[k].height, cb[k].height) &&
                              ca[k].flags == cb[k].flags && ca[k].material == cb[k].material &&
                              ca[k].blend == cb[k].blend &&
                              seas_test::same(ca[k].waterLevel, cb[k].waterLevel);
            c.cellsChanged += same ? 0 : 1;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: seas_coast_places_test <data/dev/places.json>\n");
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    const World on = worldOf(true);
    const World off = worldOf(false);
    std::printf("worlds in %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    const terrain::dev::PlacesFile file = terrain::dev::loadPlaces(argv[1]);
    int places = 0;
    for (const terrain::dev::Place& p : file.places) {
        if (p.name.rfind("cote_", 0) != 0 && p.name.rfind("stack_", 0) != 0) {
            continue;
        }
        Changes c;
        samplesAround(on, off, p.x, p.z, c);
        for (int lod = 0; lod <= 3; ++lod) {
            ++c.tiles;
            c.tilesChanged += seas_test::sameTile(seas_test::tileAt(*on.tiles, p.x, p.z, lod),
                                                  seas_test::tileAt(*off.tiles, p.x, p.z, lod))
                                  ? 0
                                  : 1;
        }
        horizonAround(on, off, p.x, p.z, c);
        std::printf("%-22s samples %zu (%zu changed), tiles %zu (%zu changed), horizon cells %zu "
                    "(%zu changed)\n",
                    p.name.c_str(), c.samples, c.samplesChanged, c.tiles, c.tilesChanged, c.cells,
                    c.cellsChanged);
        TEST_CHECK(c.samplesChanged == 0 && c.tilesChanged == 0 && c.cellsChanged == 0);
        ++places;
    }
    std::printf("%d places of the coasts in %.1f s\n", places,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    TEST_CHECK(places >= 9);
    return test::result();
}
