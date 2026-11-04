// The layer of the volcanoes on the dev map (agent 45, map_volcanoes.hpp): among the default
// layers (the map of Unreal lists it); every volcano of the planet takes at least a pixel of its
// whole map; at 64 m a pixel, it tints the ground the volcanoes made, and nothing beyond their
// reach; the same bytes whatever the threads.
#include "terrain/map/map_layers.hpp"
#include "terrain/map/map_render.hpp"
#include "terrain/map/map_source.hpp"
#include "terrain/map/map_style.hpp"
#include "terrain/map/map_volcanoes.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"
#include "volcano_zone_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

void testDefault() {
    const std::vector<std::string> names = terrain::defaultMapLayers().names();
    TEST_CHECK(std::find(names.begin(), names.end(), terrain::kMapVolcanoesLayer) != names.end());
}

// The samples of a frame.
std::vector<terrain::MapSample> samplesOf(const terrain::MapWorldSource& source,
                                          const terrain::MapFrame& frame, int threads) {
    std::vector<terrain::MapSample> out(terrain::mapGridSize(frame));
    terrain::MapSampleStats stats;
    source.sample(frame, out, threads, stats, {});
    return out;
}

// Every volcano of the planet marked on its whole map, within a pixel of its center.
void testWhole(const terrain::MapWorldSource& source, const terrain::Volcanoes& v) {
    const terrain::WorldSize& w = v.world();
    const terrain::MapFrame frame(terrain::MapBox{-0.5 * w.x, -0.5 * w.z, w.x, w.z}, 1832, 1068,
                                  w);
    const std::vector<terrain::MapSample> s = samplesOf(source, frame, 4);
    const std::size_t columns = static_cast<std::size_t>(frame.width()) + 2;
    int marked = 0;
    for (const terrain::VolcanoSite& site : v.sites()) {
        const auto i = static_cast<std::int64_t>(
            std::floor((site.x + 0.5 * w.x) / frame.stepX()));
        const auto j = static_cast<std::int64_t>(
            std::floor((site.z + 0.5 * w.z) / frame.stepZ()));
        bool found = false;
        for (std::int64_t dj = -1; dj <= 1 && !found; ++dj) {
            for (std::int64_t di = -1; di <= 1 && !found; ++di) {
                const std::int64_t r = std::clamp<std::int64_t>(j + dj, 0, frame.height() - 1);
                const std::int64_t c = std::clamp<std::int64_t>(i + di, 0, frame.width() - 1);
                const terrain::MapSample& m =
                    s[static_cast<std::size_t>(r + 1) * columns + static_cast<std::size_t>(c + 1)];
                found = m.volcano != 0 && m.volcanic != 0;
            }
        }
        marked += found ? 1 : 0;
    }
    std::printf("the whole map at %.0f m a pixel: %d of %zu volcanoes marked\n", frame.stepX(),
                marked, v.sites().size());
    TEST_CHECK(marked == static_cast<int>(v.sites().size()));
}

// Around a stratovolcano at 64 m a pixel: the ground it made tinted, nothing far from it, the same
// samples on 1 and 4 threads.
void testNear(const terrain::TerrainTiles& tiles, const terrain::MapWorldSource& source,
              const terrain::Volcanoes& v) {
    const terrain::VolcanoSite* s = nullptr;
    for (const terrain::VolcanoSite& site : v.sites()) {
        if (site.form == terrain::VolcanoForm::Strato && !site.atSea) {
            s = &site;
            break;
        }
    }
    TEST_CHECK(s != nullptr);
    if (s == nullptr) {
        return;
    }
    const double side = 4.0 * static_cast<double>(s->reach);
    const terrain::MapFrame frame(terrain::MapBox{s->x - 0.5 * side, s->z - 0.5 * side, side, side},
                                  256, 256, v.world());
    const std::vector<terrain::MapSample> one = samplesOf(source, frame, 1);
    const std::vector<terrain::MapSample> four = samplesOf(source, frame, 4);
    TEST_CHECK(one == four);
    int tinted = 0;
    int beyond = 0;
    const std::size_t columns = 258;
    for (int j = 0; j < 256; ++j) {
        for (int i = 0; i < 256; ++i) {
            const terrain::MapSample& m =
                one[static_cast<std::size_t>(j + 1) * columns + static_cast<std::size_t>(i + 1)];
            const bool far = !volcano_zone::inZone(tiles.heightfield(), frame.pixelX(i),
                                                   frame.pixelZ(j));
            tinted += m.volcanic != 0 ? 1 : 0;
            beyond += far && (m.volcanic != 0 || m.volcano != 0) ? 1 : 0;
        }
    }
    std::printf("around a stratovolcano: %d pixels tinted, %d beyond the reach of every "
                "volcano\n",
                tinted, beyond);
    TEST_CHECK(tinted > 2000 && beyond == 0);
}

} // namespace

int main() {
    testDefault();
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const terrain::MapWorldSource source(tiles);
    const terrain::Volcanoes& v = *tiles.heightfield().volcanoes();
    testWhole(source, v);
    testNear(tiles, source, v);
    return test::result();
}
