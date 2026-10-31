// The water of the world at given points (phase 11, task 7b): what the core puts there and what
// it hands to the renderers. For each point (x, z), meters: the relief and the ground with the
// water, the kind and the level of the water over the column (HeightSample), the valleys of the
// rivers (Hydrology::riversAt) and the lake (Hydrology::lakeAt) there, the levels of the vertices
// of the tiles of LOD 0 to 3 holding the point within --radius meters (TileData::waterLevels, as
// Unreal reads them in its UV channel 0), the sample of the horizon level 0 nearest to it (8 m
// lattice, HorizonCell::waterLevel), and the falls whose lip lies within --falls meters
// (Hydrology::reach). Without --planet: the relief and the sea only, no rivers nor lakes.
// Usage: terrain_probe_water [--planet <file>] [--seed <seed>] [--world <x> <z>]
//        [--threads <n>] [--radius <meters>] [--falls <meters>] <x> <z> [<x> <z>]...

#include "terrain/chunk_coord.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kHorizonPitch = 8.0; // meters between the samples of the horizon level 0

struct Options {
    std::filesystem::path planet;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    int threads = 0;
    double radius = 6.0;    // of the vertices of the tiles around a point
    double falls = 1000.0;  // of the lips of the falls around a point
    std::vector<std::pair<double, double>> points;
};

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

bool fail(const char* what, const char* text) {
    std::fprintf(stderr, "Invalid %s '%s'\n", what, text);
    return false;
}

bool parseOption(int argc, char** argv, int& i, Options& o) {
    const std::string flag = argv[i];
    const auto need = [&](int count) { return i + count < argc; };
    if (flag == "--planet" && need(1)) {
        o.planet = argv[++i];
        return true;
    }
    if (flag == "--seed" && need(1)) {
        return parse(argv[++i], o.seed) || fail("seed", argv[i]);
    }
    if (flag == "--world" && need(2)) {
        i += 2;
        return (parse(argv[i - 1], o.world.x) && parse(argv[i], o.world.z)) ||
               fail("world", argv[i - 1]);
    }
    if (flag == "--threads" && need(1)) {
        return (parse(argv[++i], o.threads) && o.threads >= 0) || fail("threads", argv[i]);
    }
    if (flag == "--radius" && need(1)) {
        return (parse(argv[++i], o.radius) && o.radius > 0.0) || fail("radius", argv[i]);
    }
    if (flag == "--falls" && need(1)) {
        return (parse(argv[++i], o.falls) && o.falls >= 0.0) || fail("falls", argv[i]);
    }
    if (flag.size() > 1 && flag[0] == '-' && (flag[1] < '0' || flag[1] > '9')) {
        return fail("option", argv[i]);
    }
    std::pair<double, double> p;
    if (!need(1) || !parse(argv[i], p.first) || !parse(argv[i + 1], p.second)) {
        return fail("point", argv[i]);
    }
    o.points.push_back(p);
    ++i;
    return true;
}

bool parseOptions(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        if (!parseOption(argc, argv, i, o)) {
            return false;
        }
    }
    if (o.points.empty()) {
        return fail("point", "none");
    }
    try {
        terrain::validate(o.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", "(multiples of 65,536 m expected)");
    }
    return true;
}

const char* kindName(terrain::WaterKind kind) {
    switch (kind) {
    case terrain::WaterKind::Ocean:
        return "sea";
    case terrain::WaterKind::River:
        return "river";
    case terrain::WaterKind::Lake:
        return "lake";
    case terrain::WaterKind::SaltLake:
        return "salt lake";
    case terrain::WaterKind::None:
        break;
    }
    return "dry";
}

void printColumn(const terrain::Heightfield& field, const terrain::Heightfield& relief, double x,
                 double z) {
    const terrain::HeightSample s = field.sampleAt(x, z);
    const double ground = relief.heightAt(x, z);
    std::printf("  relief %.2f m, ground %.2f m; water: %s", ground,
                static_cast<double>(s.height), kindName(s.water));
    if (s.water != terrain::WaterKind::None) {
        std::printf(" at %.2f m, %.2f m deep", static_cast<double>(s.waterLevel),
                    static_cast<double>(s.waterLevel - s.height));
    }
    std::printf("\n");
    const terrain::Hydrology* h = field.hydrology();
    if (h == nullptr) {
        return;
    }
    const terrain::RiverCarve c = h->riversAt(x, z, ground);
    std::printf("  rivers: carved %s, valley %.2f m, wet %s, level %.2f m, floor %.2f m, bank "
                "%.2f m, pond %.2f m, reach %lld\n",
                c.carved ? "yes" : "no", c.height, c.wet ? "yes" : "no",
                static_cast<double>(c.level), static_cast<double>(c.floor), c.bank,
                static_cast<double>(c.pond),
                c.reach == static_cast<std::size_t>(-1) ? -1LL : static_cast<long long>(c.reach));
    const terrain::LakeWater lake = h->lakeAt(x, z, ground);
    if (lake.lake < 0) {
        std::printf("  lake: none\n");
    } else {
        std::printf("  lake: %d, a %s at %.2f m\n", lake.lake, kindName(lake.kind),
                    static_cast<double>(lake.level));
    }
}

// The vertices of the tile of each LOD holding the point, within radius of it (horizontally).
void printTiles(const terrain::TerrainTiles& tiles, double x, double z, double radius) {
    const terrain::ChunkCoord chunk =
        terrain::wrapChunk(terrain::worldToChunk(x, z), tiles.world());
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileKey key = terrain::tileContaining(chunk, lod);
        const terrain::TileData data = tiles.generate({key, 0});
        const double px = terrain::nearestImage(x, data.originX, tiles.world().x);
        const double pz = terrain::nearestImage(z, data.originZ, tiles.world().z);
        int near = 0;
        int wet = 0;
        float low = std::numeric_limits<float>::infinity();
        float high = -std::numeric_limits<float>::infinity();
        for (std::size_t v = 0; v < data.mesh.vertices.size(); ++v) {
            const terrain::Vec3f& p = data.mesh.vertices[v];
            const double dx = data.originX + static_cast<double>(p.x) - px;
            const double dz = data.originZ + static_cast<double>(p.z) - pz;
            if (dx * dx + dz * dz > radius * radius) {
                continue;
            }
            ++near;
            const float level = data.waterLevels[v];
            if (level > terrain::kNoWaterLevel) {
                ++wet;
                low = std::min(low, level);
                high = std::max(high, level);
            }
        }
        std::printf("  tile LOD %d (%d, %d): %d of %d vertices within %.0f m wet", lod,
                    key.origin.x, key.origin.z, wet, near, radius);
        if (wet > 0) {
            std::printf(", levels %.2f to %.2f m", static_cast<double>(low),
                        static_cast<double>(high));
        }
        std::printf("\n");
    }
}

void printHorizon(const terrain::Heightfield& field, double x, double z) {
    const double hx = kHorizonPitch * std::round(x / kHorizonPitch);
    const double hz = kHorizonPitch * std::round(z / kHorizonPitch);
    const terrain::HeightSample s = field.sampleAt(hx, hz);
    std::printf("  horizon level 0 sample (%.0f, %.0f): %s", hx, hz, kindName(s.water));
    if (s.water != terrain::WaterKind::None) {
        std::printf(" at %.2f m", static_cast<double>(s.waterLevel));
    }
    std::printf(", ground %.2f m\n", static_cast<double>(s.height));
}

void printFalls(const terrain::Hydrology& h, const terrain::WorldSize& world, double x, double z,
                double radius) {
    for (const std::size_t r : h.reachesNear(x, z, radius)) {
        const terrain::RiverReach reach = h.reach(r);
        for (const terrain::RiverFall& f : reach.falls) {
            const double dx = terrain::nearestImage(f.x, x, world.x) - x;
            const double dz = terrain::nearestImage(f.z, z, world.z) - z;
            const double d = terrain::dmath::hypot(dx, dz);
            if (d > radius) {
                continue;
            }
            std::printf("  fall of reach %zu: lip (%.1f, %.1f), %.1f m away, water %.2f m above "
                        "and %.2f m below (%.2f m), %.1f m wide\n",
                        r, terrain::wrapCoordinate(f.x, world.x),
                        terrain::wrapCoordinate(f.z, world.z), d, static_cast<double>(f.top),
                        static_cast<double>(f.bottom), static_cast<double>(f.top - f.bottom),
                        static_cast<double>(f.width));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s [--planet <file>] [--seed <seed>] [--world <x> <z>] "
                     "[--threads <n>] [--radius <meters>] [--falls <meters>] "
                     "<x> <z> [<x> <z>]...\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const terrain::Heightfield relief(o.seed, terrain::HeightfieldParams{}, o.world);
    std::shared_ptr<const terrain::Hydrology> hydrology;
    if (!o.planet.empty()) {
        terrain::PlanetParams params;
        params.threads = o.threads;
        bool built = false;
        auto planet = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
            o.planet, o.seed, terrain::HeightfieldParams{}, params, o.world, &built));
        std::printf("planet level %s %s\n", built ? "built and written to" : "read from",
                    o.planet.string().c_str());
        hydrology = std::make_shared<const terrain::Hydrology>(o.seed, relief, planet);
    } else {
        std::printf("no planet file: the relief and the sea only, no rivers nor lakes\n");
    }
    const terrain::TerrainTiles tiles(o.seed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt, o.world, hydrology);
    const terrain::Heightfield& field = tiles.heightfield();
    for (const auto& [x, z] : o.points) {
        std::printf("point (%.1f, %.1f):\n", x, z);
        printColumn(field, relief, x, z);
        printTiles(tiles, x, z, o.radius);
        printHorizon(field, x, z);
        if (hydrology != nullptr && o.falls > 0.0) {
            printFalls(*hydrology, o.world, x, z, o.falls);
        }
    }
    return EXIT_SUCCESS;
}
