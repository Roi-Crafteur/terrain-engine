// The cost of the volcanoes (agent 45), paired measures (cost_ratio_support.hpp): out of their reach,
// rows of samples and tiles of LOD 0 to 3 cost at most 1.3 times what they cost without them (one
// bucket read); on them, the tiles of LOD 0 and their samples are measured and printed. And the
// construction of the default Heightfield with and without them (their placement).
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cost_ratio_support.hpp"
#include "test_assert.hpp"
#include "volcano_zone_support.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

struct Place {
    double x = 0.0;
    double z = 0.0;
};

terrain::HeightfieldParams without() {
    terrain::HeightfieldParams p;
    p.volcanoes.enabled = false;
    return p;
}

struct Worlds {
    terrain::TerrainTiles with{terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {}, nullptr, true,
                               0};
    terrain::TerrainTiles off{terrain::kDefaultWorldSeed, without(), {}, std::nullopt, {}, nullptr,
                              true, 0};
};

// Places a little beyond the reach of every 61st volcano (where a sample still reads a bucket that
// holds volcanoes), whose square of side meters keeps out of every reach.
std::vector<Place> beyond(const terrain::Heightfield& field, double side, std::size_t wanted) {
    const terrain::Volcanoes& v = *field.volcanoes();
    std::vector<Place> out;
    for (std::size_t i = 0; i < v.sites().size() && out.size() < wanted; i += 61) {
        const terrain::VolcanoSite& s = v.sites()[i];
        const double r = static_cast<double>(s.reach) + side + 200.0;
        const Place p{s.x + r * std::cos(0.7 * static_cast<double>(i)),
                      s.z + r * std::sin(0.7 * static_cast<double>(i))};
        bool clear = true;
        for (double dz = 0.0; dz <= side && clear; dz += 32.0) {
            for (double dx = 0.0; dx <= side && clear; dx += 32.0) {
                clear = !volcano_zone::inZone(field, p.x + dx, p.z + dz);
            }
        }
        if (clear) {
            out.push_back(p);
        }
    }
    return out;
}

terrain::TileKey keyAt(double x, double z, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    return terrain::tileContaining(chunk, lod);
}

cost_test::Ratio rows(const Worlds& w, const std::vector<Place>& places, double most,
                      const char* where) {
    const cost_test::TimedRun run = [&](cost_test::Side side, std::size_t item) {
        const terrain::TerrainTiles& t = side == cost_test::Side::With ? w.with : w.off;
        std::vector<double> xs;
        std::vector<double> zs;
        for (int i = 0; i < 1024; ++i) {
            xs.push_back(places[item].x + 2.0 * (i % 32));
            zs.push_back(places[item].z + 2.0 * (i / 32));
        }
        std::vector<float> out(xs.size());
        return cost_test::secondsOf([&] { t.heightfield().heightsAt(xs, zs, out); });
    };
    const cost_test::Ratio ratio = cost_test::ratioAtMost(places.size(), run, most);
    std::printf("patches %-22s %s\n", where, cost_test::ratioText(ratio).c_str());
    return ratio;
}

cost_test::Ratio tiles(const Worlds& w, const std::vector<Place>& places, double most,
                       const char* where) {
    const cost_test::TimedRun run = [&](cost_test::Side side, std::size_t item) {
        const terrain::TerrainTiles& t = side == cost_test::Side::With ? w.with : w.off;
        const terrain::TileKey key =
            keyAt(places[item].x, places[item].z, static_cast<int>(item % 4));
        return cost_test::secondsOf([&] { (void)t.generate({key, 0}); });
    };
    const cost_test::Ratio ratio = cost_test::ratioAtMost(places.size(), run, most);
    std::printf("tiles   %-22s %s\n", where, cost_test::ratioText(ratio).c_str());
    return ratio;
}

// The time of the tiles of LOD 0 on the summits, the flanks and the fields of volcanoes on land.
void onVolcanoes(const Worlds& w) {
    const terrain::Volcanoes& v = *w.with.heightfield().volcanoes();
    std::vector<Place> places;
    for (std::size_t i = 0; i < v.sites().size() && places.size() < 12; i += 97) {
        const terrain::VolcanoSite& s = v.sites()[i];
        if (s.atSea) {
            continue;
        }
        const terrain::VolcanoOffset top = terrain::volcanoSummit(s);
        const double r = 0.3 * static_cast<double>(s.radius) * static_cast<double>(places.size() % 3);
        places.push_back({s.x + top.dx + r, s.z + top.dz});
    }
    double with = 0.0;
    double off = 0.0;
    for (const Place& p : places) {
        const terrain::TileKey key = keyAt(p.x, p.z, 0);
        (void)w.with.generate({key, 0});
        with += cost_test::secondsOf([&] { (void)w.with.generate({key, 0}); });
        off += cost_test::secondsOf([&] { (void)w.off.generate({key, 0}); });
    }
    const auto n = static_cast<double>(places.size());
    std::printf("tiles of LOD 0 on %zu volcanoes: %.2f ms each with them, %.2f ms without (x%.2f)\n",
                places.size(), 1000.0 * with / n, 1000.0 * off / n, with / off);
    (void)rows(w, places, 1e9, "on volcanoes");
}

void construction() {
    const auto start = std::chrono::steady_clock::now();
    const terrain::Heightfield on(terrain::kDefaultWorldSeed + 1);
    const double a = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto again = std::chrono::steady_clock::now();
    const terrain::Heightfield off(terrain::kDefaultWorldSeed + 1, without());
    const double b = std::chrono::duration<double>(std::chrono::steady_clock::now() - again).count();
    std::printf("a Heightfield of another seed: %.2f s with its volcanoes (%zu, %.2f MB), %.2f s "
                "without (its geography shared)\n",
                a, on.volcanoes()->sites().size(),
                static_cast<double>(on.volcanoes()->memoryBytes()) / 1.0e6, b);
}

} // namespace

int main() {
    const Worlds w;
    const std::vector<Place> clear = beyond(w.with.heightfield(), 128.0, 12);
    TEST_CHECK(clear.size() >= 8);
    TEST_CHECK(rows(w, clear, 1.3, "beyond the volcanoes").passed());
    TEST_CHECK(tiles(w, clear, 1.3, "beyond the volcanoes").passed());
    onVolcanoes(w);
    construction();
    return test::result();
}
