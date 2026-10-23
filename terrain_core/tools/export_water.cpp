// Maps and views of the water of a world (phase 11B): the sea where the geography puts it and the
// dry depressions below the sea level. Writes world.png (the whole world in cells of cell-size
// meters), map_<name>_<km>km.png around each place (2,048, 200 and 20 km wide, 1,024 px) and
// view_<name>.png, a view at the height of a walker from each place given by --view (its eye
// 1.8 m above the ground, heading in degrees clockwise from the north, -Z), and prints, and writes
// to water.txt, the depressions and where each view stands. Without --map and --view: the deepest
// depression, its map, and a view from its floor toward its steepest side.
// Usage: terrain_export_water <output-dir> [--seed <seed>] [--world <x> <z>] [--threads <n>]
//        [--cell <meters>] [--width <pixels>] [--pixels <map-pixels>] [--map <name> <x> <z>]...
//        [--view <name> <x> <z> <heading-degrees>]...

#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/relief_png.hpp"
#include "terrain/io/relief_view.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr double kEyeAboveGround = 1.8;
constexpr std::array<double, 3> kMapKilometers = {2048.0, 200.0, 20.0};
constexpr double kTowerHeight = 330.0;
constexpr double kTowerDistance = 2000.0;

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
    double heading = 0.0; // degrees, views only
    double tilt = -0.02;  // of the view
};

struct Options {
    std::string directory;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    int threads = 0;
    double cellSize = 16384.0;
    int width = 1600;
    int pixels = 1024; // of the maps
    std::vector<Place> maps;
    std::vector<Place> views;
};

// Printed and kept for water.txt.
class Report {
public:
    void add(const char* format, ...) {
        std::array<char, 1024> line{};
        va_list args;
        va_start(args, format);
        std::vsnprintf(line.data(), line.size(), format, args);
        va_end(args);
        std::fputs(line.data(), stdout);
        text_ += line.data();
    }
    [[nodiscard]] bool write(const std::filesystem::path& path) const {
        std::ofstream out(path);
        out << text_;
        return static_cast<bool>(out);
    }

private:
    std::string text_;
};

bool fail(const char* what, const char* text) {
    std::fprintf(stderr, "Invalid %s '%s'\n", what, text);
    return false;
}

// The option at argv[i] with its count values, i moved past them.
bool parseOption(int argc, char** argv, int& i, Options& o) {
    const std::string flag = argv[i];
    const auto need = [&](int count) { return i + count < argc; };
    bool ok = true;
    if (flag == "--seed" && need(1)) {
        ok = parse(argv[++i], o.seed) || fail("seed", argv[i]);
    } else if (flag == "--world" && need(2)) {
        ok = (parse(argv[i + 1], o.world.x) && parse(argv[i + 2], o.world.z)) ||
             fail("world", argv[i + 1]);
        i += 2;
    } else if (flag == "--threads" && need(1)) {
        ok = (parse(argv[++i], o.threads) && o.threads >= 0) || fail("threads", argv[i]);
    } else if (flag == "--cell" && need(1)) {
        ok = (parse(argv[++i], o.cellSize) && o.cellSize > 0.0) || fail("cell size", argv[i]);
    } else if (flag == "--width" && need(1)) {
        ok = (parse(argv[++i], o.width) && o.width >= 16) || fail("width", argv[i]);
    } else if (flag == "--pixels" && need(1)) {
        ok = (parse(argv[++i], o.pixels) && o.pixels >= 16) || fail("pixels", argv[i]);
    } else if (flag == "--map" && need(3)) {
        Place p{argv[i + 1]};
        ok = (parse(argv[i + 2], p.x) && parse(argv[i + 3], p.z)) || fail("map", argv[i + 1]);
        o.maps.push_back(p);
        i += 3;
    } else if (flag == "--view" && need(4)) {
        Place p{argv[i + 1]};
        ok = (parse(argv[i + 2], p.x) && parse(argv[i + 3], p.z) && parse(argv[i + 4], p.heading)) ||
             fail("view", argv[i + 1]);
        o.views.push_back(p);
        i += 4;
    } else {
        ok = fail("option", argv[i]);
    }
    return ok;
}

bool parseOptions(int argc, char** argv, Options& o) {
    if (argc < 2 || argv[1][0] == '-') {
        return false;
    }
    o.directory = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (!parseOption(argc, argv, i, o)) {
            return false;
        }
    }
    try {
        terrain::validate(o.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", "(multiples of 65,536 m expected)");
    }
    if (std::fmod(o.world.x, o.cellSize) != 0.0 || std::fmod(o.world.z, o.cellSize) != 0.0) {
        return fail("cell size", "(it must divide both sides of the world)");
    }
    return true;
}

bool writeMaps(const terrain::Heightfield& field, const Options& o, const Place& place,
               const std::filesystem::path& directory, Report& r) {
    bool written = true;
    for (const double km : kMapKilometers) {
        const terrain::ReliefGrid grid = terrain::sampleReliefWindow(
            field, place.x, place.z, km * 1000.0 / o.pixels, o.pixels, o.pixels, o.threads);
        const std::string file =
            "map_" + place.name + "_" + std::to_string(static_cast<int>(km)) + "km.png";
        written = terrain::writePng(directory / file, terrain::renderRelief(grid)) && written;
        r.add("%s: %.0f km around (%.0f, %.0f) m\n", file.c_str(), km, place.x, place.z);
    }
    return written;
}

bool writeView(const terrain::Heightfield& field, const Options& o, const Place& place,
               const std::filesystem::path& directory, Report& r) {
    const terrain::HeightSample at = field.sampleAt(place.x, place.z);
    terrain::ViewParams p;
    p.eyeX = place.x;
    p.eyeZ = place.z;
    p.eyeHeight = static_cast<double>(std::max(at.height, at.waterLevel)) + kEyeAboveGround;
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
    const terrain::ViewWater water = [&field](double x, double z) {
        const terrain::HeightSample s = field.sampleAt(x, z);
        return s.water == terrain::WaterKind::None ? -std::numeric_limits<float>::infinity()
                                                   : s.waterLevel;
    };
    const std::string file = "view_" + place.name + ".png";
    r.add("%s: eye at x %.0f m, z %.0f m, ground %.1f m, water %s, heading %.1f degrees\n",
          file.c_str(), place.x, place.z, static_cast<double>(at.height),
          at.water == terrain::WaterKind::None ? "none" : "yes", place.heading);
    return terrain::writePng(directory / file,
                             terrain::renderReliefView(ground, water, p, o.threads));
}

// The deepest depression, a map around it, a view from its floor toward its escarpment and one
// from the top of the escarpment back over the floor, looking down a little.
void addDeepest(const terrain::Heightfield& field, Options& o, Report& r) {
    const std::vector<terrain::Depression> all = field.depressions();
    if (all.empty()) {
        return;
    }
    const terrain::Depression d = *std::min_element(
        all.begin(), all.end(), [](const auto& a, const auto& b) { return a.floor < b.floor; });
    const double dx = terrain::dmath::cosTurns(d.escarpment);
    const double dz = terrain::dmath::sinTurns(d.escarpment);
    // The heading of the escarpment from the center: turns from -Z, clockwise.
    const double heading = std::fmod(360.0 * (d.escarpment + 0.25) + 360.0, 360.0);
    double reach = 0.0;
    while (reach < 3.0 * d.radius && field.heightAt(d.x + reach * dx, d.z + reach * dz) < -5.0f) {
        reach += 100.0;
    }
    r.add("deepest depression: floor %.0f m at (%.0f, %.0f) m, radius %.0f km, its escarpment "
          "%.1f km away heading %.1f degrees\n",
          static_cast<double>(d.floor), d.x, d.z, d.radius / 1000.0, reach / 1000.0, heading);
    o.maps.push_back({"depression", d.x, d.z, 0.0});
    o.maps.push_back({"escarpment", d.x + reach * dx, d.z + reach * dz, 0.0});
    const double floorReach = std::max(0.0, reach - 6000.0);
    o.views.push_back(
        {"depression", d.x + floorReach * dx, d.z + floorReach * dz, heading, 0.02});
    o.views.push_back({"depression_rim", d.x + (reach + 200.0) * dx, d.z + (reach + 200.0) * dz,
                       std::fmod(heading + 180.0, 360.0), -0.06});
}

void reportDepressions(const terrain::Heightfield& field, Report& r) {
    const std::vector<terrain::Depression> all = field.depressions();
    r.add("%zu depressions below the sea level:\n", all.size());
    for (const terrain::Depression& d : all) {
        r.add("  floor %4.0f m at (%9.0f, %9.0f) m, radius %2.0f km\n",
              static_cast<double>(d.floor), d.x, d.z, d.radius / 1000.0);
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [--seed <seed>] [--world <x> <z>] [--threads <n>] "
                     "[--cell <meters>] [--width <pixels>] [--pixels <map-pixels>] "
                     "[--map <name> <x> <z>]... "
                     "[--view <name> <x> <z> <heading-degrees>]...\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const std::filesystem::path directory = o.directory;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const terrain::Heightfield field(o.seed, terrain::HeightfieldParams{}, o.world);
    Report r;
    r.add("water of seed %llu, world %.0f x %.0f km\n", static_cast<unsigned long long>(o.seed),
          o.world.x / 1000.0, o.world.z / 1000.0);
    reportDepressions(field, r);
    if (o.maps.empty() && o.views.empty()) {
        addDeepest(field, o, r);
    }
    bool written = terrain::writePng(
        directory / "world.png",
        terrain::renderRelief(terrain::sampleReliefWorld(field, o.cellSize, o.threads)));
    for (const Place& place : o.maps) {
        written = writeMaps(field, o, place, directory, r) && written;
    }
    for (const Place& place : o.views) {
        written = writeView(field, o, place, directory, r) && written;
    }
    if (!written || !r.write(directory / "water.txt")) {
        std::fprintf(stderr, "Failed to write the maps into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
