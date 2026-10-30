// Maps and views of the rivers and lakes of a world (phase 11B): the planet level (read from
// --planet, or built and written there), its hydrology over the relief, and around each place
// maps of 2,048, 200 and 20 km (map_<name>_<km>km.png, 1,024 px) and view_<name>.png, a view at
// the height of a walker (1.8 m above the ground or the water, heading in degrees clockwise from
// the north, -Z, optionally above meters higher). Prints, and writes to rivers.txt, where each
// view stands, the deepest cuts and the lakes of the mountains, and the points of the windows of
// mountain_freeze_test that the water changes. Without --map and --view: the window of high
// mountains at (7,340,032, -7,143,424) (map_high_256km.png) and a lake of a high basin there;
// around (-6,291,456, 196,608) a river at the bottom of a carved valley, a gorge through a crest
// and a waterfall; and a lowland river with its meanders.
// --tilted-view gives the tilt of the view too (the tangent of the elevation of its middle row);
// --without coasts draws the world without the coasts of agent 33 (the same planet level).
// Usage: terrain_export_rivers <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>]
//        [--threads <n>] [--width <pixels>] [--pixels <map-pixels>] [--map <name> <x> <z>]...
//        [--view <name> <x> <z> <heading-degrees> <above-meters>]...
//        [--tilted-view <name> <x> <z> <heading-degrees> <above-meters> <tilt>]...
//        [--without coasts]

#include "rivers_places.hpp"

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/relief_png.hpp"
#include "terrain/io/relief_view.hpp"
#include "terrain/storage/planet_file.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using rivers_tool::Options;
using rivers_tool::Place;
using rivers_tool::Report;

constexpr double kEyeAboveGround = 1.8;
constexpr std::array<double, 3> kMapKilometers = {2048.0, 200.0, 20.0};
constexpr double kTowerHeight = 330.0;
constexpr double kTowerDistance = 2000.0;
constexpr double kHighX = 7'340'032.0;
constexpr double kHighZ = -7'143'424.0;
// Mountains whose valleys the rivers carve (the high window is mostly a lake of a high basin).
constexpr double kRiversX = -6'291'456.0;
constexpr double kRiversZ = 196'608.0;

bool writeMap(const terrain::Heightfield& field, const Options& o, const std::string& file,
              double x, double z, double km, Report& r) {
    const terrain::ReliefGrid grid =
        terrain::sampleReliefWindow(field, x, z, km * 1000.0 / o.pixels, o.pixels, o.pixels,
                                    o.threads);
    r.add("%s: %.0f km around (%.0f, %.0f) m\n", file.c_str(), km, x, z);
    return terrain::writePng(o.directory / file, terrain::renderRelief(grid));
}

terrain::ViewWater waterOf(const terrain::Heightfield& field) {
    return [&field](double x, double z) {
        const terrain::HeightSample s = field.sampleAt(x, z);
        return s.water == terrain::WaterKind::None ? -std::numeric_limits<float>::infinity()
                                                   : s.waterLevel;
    };
}

bool writeView(const terrain::Heightfield& field, const Options& o, const Place& place,
               Report& r) {
    const terrain::HeightSample at = field.sampleAt(place.x, place.z);
    const bool wet = at.water != terrain::WaterKind::None && at.waterLevel > at.height;
    terrain::ViewParams p;
    p.eyeX = place.x;
    p.eyeZ = place.z;
    p.eyeHeight = static_cast<double>(wet ? at.waterLevel : at.height) + kEyeAboveGround +
                  place.above;
    p.heading = place.heading / 360.0;
    p.tilt = place.tilt;
    p.width = o.width;
    p.height = o.width * 9 / 16;
    p.towerX = place.x + kTowerDistance * terrain::dmath::sinTurns(p.heading - 0.03);
    p.towerZ = place.z - kTowerDistance * terrain::dmath::cosTurns(p.heading - 0.03);
    p.towerHeight = kTowerHeight;
    const terrain::ViewGround ground = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    const std::string file = "view_" + place.name + ".png";
    r.add("%s: eye at x %.0f m, z %.0f m, ground %.1f m, water %s, %.0f m above it, heading "
          "%.1f degrees%s%s\n",
          file.c_str(), place.x, place.z, static_cast<double>(at.height),
          wet ? std::to_string(static_cast<int>(std::lround(at.waterLevel))).c_str() : "none",
          kEyeAboveGround + place.above, place.heading, place.note.empty() ? "" : "; ",
          place.note.c_str());
    return terrain::writePng(o.directory / file,
                             terrain::renderReliefView(ground, waterOf(field), p, o.threads));
}

bool writePlace(const terrain::Heightfield& field, const Options& o, const Place& place,
                Report& r) {
    bool written = true;
    for (const double km : kMapKilometers) {
        const std::string file =
            "map_" + place.name + "_" + std::to_string(static_cast<int>(km)) + "km.png";
        written = writeMap(field, o, file, place.x, place.z, km, r) && written;
    }
    return writeView(field, o, place, r) && written;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!rivers_tool::parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [--planet <file>] [--seed <seed>] [--world <x> <z>] "
                     "[--threads <n>] [--width <pixels>] [--pixels <map-pixels>] "
                     "[--map <name> <x> <z>]... "
                     "[--view <name> <x> <z> <heading-degrees> <above-meters>]... "
                     "[--tilted-view <name> <x> <z> <heading-degrees> <above-meters> <tilt>]... "
                     "[--without coasts]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    std::error_code error;
    std::filesystem::create_directories(o.directory, error);
    const auto start = std::chrono::steady_clock::now();
    terrain::PlanetParams params;
    params.threads = o.threads;
    bool built = false;
    const std::filesystem::path planetFile =
        o.planet.empty() ? o.directory / terrain::kPlanetFileName : o.planet;
    auto planet = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
        planetFile, o.seed, terrain::HeightfieldParams{}, params, o.world, &built));
    terrain::HeightfieldParams reliefParams;
    reliefParams.geography.coasts = o.coasts;
    const terrain::Heightfield relief(o.seed, reliefParams, o.world);
    auto hydrology = std::make_shared<const terrain::Hydrology>(o.seed, relief, planet);
    const terrain::Heightfield field = relief.withHydrology(hydrology);
    Report r;
    r.add("rivers of seed %llu, world %.0f x %.0f km: planet level %s %s in %.1f s, %zu reaches, "
          "%zu lakes\n",
          static_cast<unsigned long long>(o.seed), o.world.x / 1000.0, o.world.z / 1000.0,
          built ? "built and written to" : "read from", planetFile.string().c_str(),
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
          hydrology->reachCount(), planet->lakes().size());
    bool written = true;
    if (o.maps.empty() && o.views.empty()) {
        const bool high = o.world == terrain::WorldSize{};
        const double cx = high ? kHighX : 0.0;
        const double cz = high ? kHighZ : 0.0;
        written = writeMap(field, o, "map_high_256km.png", cx, cz, 256.0, r) && written;
        rivers_tool::addPlaces(*hydrology, field, high ? kRiversX : cx, high ? kRiversZ : cz, cx,
                               cz, o, r);
        rivers_tool::reportMountains(*hydrology, field, cx, cz, r);
        if (high) {
            rivers_tool::reportMountains(*hydrology, field, kRiversX, kRiversZ, r);
        }
    }
    for (const Place& m : o.maps) {
        for (const double km : kMapKilometers) {
            const std::string file =
                "map_" + m.name + "_" + std::to_string(static_cast<int>(km)) + "km.png";
            written = writeMap(field, o, file, m.x, m.z, km, r) && written;
        }
    }
    for (const Place& v : o.views) {
        written = writePlace(field, o, v, r) && written;
    }
    if (o.world == terrain::WorldSize{}) {
        rivers_tool::reportFreeze(relief, field, r);
    }
    r.add("courses computed: %zu (%.1f MB), %.1f s in all\n", hydrology->coursesComputed(),
          static_cast<double>(hydrology->courseBytes()) / 1.0e6,
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    written = r.write(o.directory / "rivers.txt") && written;
    if (!written) {
        std::fprintf(stderr, "Could not write every file to %s\n", o.directory.string().c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
