// Views of a relief in perspective (terrain::renderReliefView), at the places that judge it:
// view_foot.png at the foot of the longest chain, 20 to 30 km from its highest point (the lowest
// place from which it is seen) and looking at it; view_plain.png in the plain farthest from the
// chains; view_summit.png from the highest summit of the planet toward the lowest ground 15 km
// around; view_above.png from 300 m above the longest chain, looking at its highest point;
// view_slope.png at a flank of the chain from 2 km away, at its height (the flank below the
// highest point toward the lowest ground 3 km from it). The eye stands 1.8 m above the ground
// (300 m for view_above), the field of view is 70 degrees wide, and a tower of 330 m stands 2 km
// away in each view as a scale mark. Prints, and writes to views.txt, where each view stands
// and what the eye at the foot sees of the highest point of the chain: the angle of its
// elevation and whether the ground hides it farther than 1 km before it (nearer, it is its own
// top). The places come from terrain::measureRelief on cells of cell-size meters; --mountain
// takes the chain around a point instead (its highest point within 12 km), the same in every
// relief compared; with --plain, view_plain_fixed.png is a plain view at that point; --without
// ravines or --without lively switches off a step of the relief of the geography
// (GeographyParams::ravines, livelyPlains), to compare them. Without the optional arguments:
// seed 20261019, the default relief, 1,600 x 900 pixels, cells of 16,384 m, all the hardware
// threads, the default world.
// Usage: terrain_export_view <output-dir> [seed] [default|planetary|legacy|geography] [width]
//        [cell-size] [threads] [world-x] [world-z] [--plain <x> <z>] [--mountain <x> <z>]
//        [--without ravines|lively]...

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/io/relief_stats.hpp"
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
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr double kEyeAboveGround = 1.8;
constexpr double kFlightAboveGround = 300.0;
constexpr double kTowerHeight = 330.0;
constexpr double kTowerDistance = 2000.0;
constexpr double kTowerAside = 0.03; // turns left of the axis
constexpr double kOwnTop = 1000.0;   // the ground this close to a summit is its own top
// The flank of view_slope: its point kFlankDistance from the summit, the eye kFlankEye farther
// and at least kFlankAbove above its ground.
constexpr double kFlankDistance = 3000.0;
constexpr double kFlankEye = 2000.0;
constexpr double kFlankAbove = 30.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    bool fixedPlain = false;
    double plainX = 0.0;
    double plainZ = 0.0;
    bool fixedMountain = false; // --mountain: the chain of the views around this point
    double mountainX = 0.0;
    double mountainZ = 0.0;
    bool ravines = true;
    bool lively = true;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    std::string relief = "default";
    int width = 1600;
    double cellSize = 16384.0;
    int threads = 0;
    terrain::WorldSize world;
};

bool fail(const char* what, const char* text, const char* expected) {
    std::fprintf(stderr, "Invalid %s '%s': expected %s\n", what, text, expected);
    return false;
}

// As terrain_export_relief: default and geography are the planetary geography, planetary the
// preset of the planet level (the same world), legacy the relief of generator 2.
terrain::HeightfieldParams reliefParams(const std::string& relief) {
    terrain::HeightfieldParams params = relief == "planetary"
                                            ? terrain::planetaryHeightfieldParams()
                                            : terrain::HeightfieldParams{};
    if (relief == "legacy" || relief == "geography") {
        params.geography.enabled = relief == "geography";
    }
    return params;
}

bool parseWorld(int argc, char** argv, Options& options) {
    if (argc > 7 && !(parse(argv[7], options.world.x) && std::isfinite(options.world.x))) {
        return fail("world x", argv[7], "a number of meters");
    }
    if (argc > 8 && !(parse(argv[8], options.world.z) && std::isfinite(options.world.z))) {
        return fail("world z", argv[8], "a number of meters");
    }
    try {
        terrain::validate(options.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", argc > 7 ? argv[7] : "default", "multiples of 65,536 m");
    }
    const double cells = std::min(options.world.x, options.world.z) / options.cellSize;
    if (std::fmod(options.world.x, options.cellSize) != 0.0 ||
        std::fmod(options.world.z, options.cellSize) != 0.0 || cells < 3.0) {
        return fail("cell size", argc > 5 ? argv[5] : "16384",
                    "meters dividing both sides of the world, at least 3 cells per side");
    }
    return true;
}

// Reads the point <x> <z> after the flag at argv[k] (--plain or --mountain).
bool parsePoint(int argc, char** argv, int k, double& x, double& z) {
    if (k + 2 >= argc || !(parse(argv[k + 1], x) && std::isfinite(x) && parse(argv[k + 2], z) &&
                           std::isfinite(z))) {
        std::fprintf(stderr, "Invalid point: expected %s <x> <z> in meters\n", argv[k]);
        return false;
    }
    return true;
}

// Moves --plain <x> <z>, --mountain <x> <z> and --without <step> out of the arguments into
// options; the other arguments stay in order in kept (the program name first).
bool parseFlags(int argc, char** argv, Options& options, std::vector<char*>& kept) {
    for (int k = 0; k < argc; ++k) {
        if (std::strcmp(argv[k], "--plain") == 0 || std::strcmp(argv[k], "--mountain") == 0) {
            const bool plain = argv[k][2] == 'p';
            if (!parsePoint(argc, argv, k, plain ? options.plainX : options.mountainX,
                            plain ? options.plainZ : options.mountainZ)) {
                return false;
            }
            (plain ? options.fixedPlain : options.fixedMountain) = true;
            k += 2;
        } else if (std::strcmp(argv[k], "--without") == 0) {
            const bool ravines = k + 1 < argc && std::strcmp(argv[k + 1], "ravines") == 0;
            const bool lively = k + 1 < argc && std::strcmp(argv[k + 1], "lively") == 0;
            if (!ravines && !lively) {
                std::fprintf(stderr, "Invalid step: expected --without ravines or lively\n");
                return false;
            }
            options.ravines = options.ravines && !ravines;
            options.lively = options.lively && !lively;
            ++k;
        } else {
            kept.push_back(argv[k]);
        }
    }
    return true;
}

// Reads the optional arguments into options. On the first invalid one, prints what is wrong
// and returns false; nothing has been computed or written then.
bool parseArguments(int argc, char** argv, Options& options) {
    if (argc < 2 || argc > 9) {
        std::fprintf(stderr, "Expected 1 to 8 arguments, got %d\n", argc - 1);
        return false;
    }
    if (argc > 2 && !parse(argv[2], options.seed)) {
        return fail("seed", argv[2], "an unsigned integer");
    }
    if (argc > 3) {
        options.relief = argv[3];
        if (options.relief != "default" && options.relief != "planetary" &&
            options.relief != "legacy" && options.relief != "geography") {
            return fail("relief", argv[3], "default, planetary, legacy or geography");
        }
    }
    if (argc > 4 &&
        !(parse(argv[4], options.width) && options.width >= 16 && options.width <= 8192)) {
        return fail("width", argv[4], "an integer from 16 to 8192");
    }
    if (argc > 5 && !(parse(argv[5], options.cellSize) && std::isfinite(options.cellSize) &&
                      options.cellSize > 0.0)) {
        return fail("cell size", argv[5], "a number of meters above 0");
    }
    if (argc > 6 && !(parse(argv[6], options.threads) && options.threads >= 0)) {
        return fail("threads", argv[6], "an integer of at least 0 (0: all the hardware threads)");
    }
    return parseWorld(argc, argv, options);
}

// What the tool prints, also kept for views.txt.
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
        std::ofstream file(path, std::ios::binary);
        file.write(text_.data(), static_cast<std::streamsize>(text_.size()));
        file.close();
        return static_cast<bool>(file);
    }

private:
    std::string text_;
};

struct Point {
    double x = 0.0;
    double z = 0.0;
    float height = 0.0f;
};

Point pointAt(const terrain::Heightfield& field, double x, double z) {
    return {x, z, field.heightAt(x, z)};
}

// The highest sample of a square of 2 half + 1 samples step meters apart around (x, z).
Point highestAround(const terrain::Heightfield& field, double x, double z, int half, double step) {
    Point best = pointAt(field, x, z);
    for (int j = -half; j <= half; ++j) {
        for (int i = -half; i <= half; ++i) {
            const Point p = pointAt(field, x + i * step, z + j * step);
            best = p.height > best.height ? p : best;
        }
    }
    return best;
}

// The summit near a cell of the grid: the highest point within 12 km at 500 m, then climbed to
// the top in steps of 100, 25 and 5 m (each repeated while it rises): crests are sharp.
Point summitNear(const terrain::Heightfield& field, double x, double z) {
    Point best = highestAround(field, x, z, 24, 500.0);
    for (const double step : {100.0, 25.0, 5.0}) {
        for (int k = 0; k < 64; ++k) {
            const Point next = highestAround(field, best.x, best.z, 4, step);
            if (!(next.height > best.height)) {
                break;
            }
            best = next;
        }
    }
    return best;
}

// The lowest land (at least 1 m) seen from (x, z) at these distances, in 64 directions; the
// point at the first distance due south when there is none.
Point lowestAround(const terrain::Heightfield& field, double x, double z,
                   std::initializer_list<double> distances) {
    Point best{x, z + *distances.begin(), 1.0e9f};
    for (const double d : distances) {
        for (int k = 0; k < 64; ++k) {
            const Point p = pointAt(field, x + d * terrain::dmath::sinTurns(k / 64.0),
                                    z - d * terrain::dmath::cosTurns(k / 64.0));
            best = p.height >= 1.0f && p.height < best.height ? p : best;
        }
    }
    if (best.height == 1.0e9f) {
        best = pointAt(field, x, z + *distances.begin());
    }
    return best;
}

// Heading in turns from (x0, z0) toward (x1, z1).
double headingTo(double x0, double z0, double x1, double z1) {
    return terrain::dmath::atan2(x1 - x0, -(z1 - z0)) / kTwoPi;
}

terrain::ViewParams viewFrom(const Options& options, const Point& eye, double above,
                             double heading, double tilt) {
    terrain::ViewParams p;
    p.eyeX = eye.x;
    p.eyeZ = eye.z;
    p.eyeHeight = static_cast<double>(eye.height) + above;
    p.heading = heading;
    p.tilt = tilt;
    p.width = options.width;
    p.height = options.width * 9 / 16;
    const double towerHeading = heading - kTowerAside;
    p.towerX = eye.x + kTowerDistance * terrain::dmath::sinTurns(towerHeading);
    p.towerZ = eye.z - kTowerDistance * terrain::dmath::cosTurns(towerHeading);
    p.towerHeight = kTowerHeight;
    return p;
}

bool writeView(const terrain::Heightfield& field, const Options& options, const char* name,
               const terrain::ViewParams& view, const std::filesystem::path& directory,
               Report& r) {
    const terrain::ViewGround ground = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    const std::string file = "view_" + std::string(name) + ".png";
    r.add("%s: eye at (%.3f, %.3f) km, %.0f m high, heading %.1f degrees, tilt %.3f, tower at "
          "(%.3f, %.3f) km: %s\n",
          name, view.eyeX / 1000.0, view.eyeZ / 1000.0, view.eyeHeight, 360.0 * view.heading,
          view.tilt, view.towerX / 1000.0, view.towerZ / 1000.0, file.c_str());
    return terrain::writePng(directory / file,
                             terrain::renderReliefView(ground, view, options.threads));
}

// The foot of the longest chain: among the land 20, 25 and 30 km from its highest point in 64
// directions, the lowest from which the ground does not hide it (else the lowest); and what it
// sees of it.
terrain::ViewParams footView(const terrain::Heightfield& field, const Options& options,
                             const Point& summit, Report& r) {
    const terrain::ViewGround ground = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    Point foot{0.0, 0.0, 1.0e9f};
    Point lowest = foot;
    int candidates = 0;
    int seeing = 0;
    for (const double d : {20'000.0, 25'000.0, 30'000.0}) {
        for (int k = 0; k < 64; ++k) {
            const Point p = pointAt(field, summit.x + d * terrain::dmath::sinTurns(k / 64.0),
                                    summit.z - d * terrain::dmath::cosTurns(k / 64.0));
            if (p.height < 1.0f) {
                continue;
            }
            ++candidates;
            lowest = p.height < lowest.height ? p : lowest;
            const terrain::ViewParams eye = viewFrom(options, p, kEyeAboveGround, 0.0, 0.0);
            if (terrain::sightline(ground, eye, summit.x, summit.z, summit.height, kOwnTop)
                    .visible) {
                ++seeing;
                foot = p.height < foot.height ? p : foot;
            }
        }
    }
    if (seeing == 0) {
        foot = candidates > 0 ? lowest : pointAt(field, summit.x, summit.z + 25'000.0);
    }
    terrain::ViewParams view =
        viewFrom(options, foot, kEyeAboveGround, headingTo(foot.x, foot.z, summit.x, summit.z),
                 0.0);
    const terrain::Sightline line =
        terrain::sightline(ground, view, summit.x, summit.z, summit.height, kOwnTop);
    view.tilt = std::max(0.0, 0.6 * line.tangent);
    r.add("highest point of the longest chain: %.0f m at (%.3f, %.3f) km, seen from %d of the "
          "%d points of land 20 to 30 km around it; from the lowest of them (%.0f m, %.1f km "
          "away) %.2f degrees above the horizontal, %s",
          static_cast<double>(summit.height), summit.x / 1000.0, summit.z / 1000.0, seeing,
          candidates, static_cast<double>(foot.height), line.distance / 1000.0,
          terrain::dmath::atan(line.tangent) * 360.0 / kTwoPi,
          line.visible ? "visible\n" : "hidden by the ground");
    if (!line.visible) {
        r.add(" %.1f km before it\n", (line.distance - line.blockedAt) / 1000.0);
    }
    return view;
}

// A flank of the chain seen from 2 km away: the point of the flank is the lowest ground 3 km
// from the summit, the eye 2 km farther from the summit, at the height of that point (at least
// kFlankAbove above its own ground), looking back at the summit.
terrain::ViewParams slopeView(const terrain::Heightfield& field, const Options& options,
                              const Point& summit) {
    const Point flank = lowestAround(field, summit.x, summit.z, {kFlankDistance});
    const double dx = flank.x - summit.x;
    const double dz = flank.z - summit.z;
    const double away = kFlankEye / terrain::dmath::hypot(dx, dz);
    const Point eye = pointAt(field, flank.x + away * dx, flank.z + away * dz);
    const double above =
        std::max(static_cast<double>(flank.height) - static_cast<double>(eye.height), kFlankAbove);
    const double eyeHeight = static_cast<double>(eye.height) + above;
    const double distance = kFlankDistance + kFlankEye;
    const double tilt =
        std::clamp(0.5 * (static_cast<double>(summit.height) - eyeHeight) / distance, 0.0, 0.4);
    return viewFrom(options, eye, above, headingTo(eye.x, eye.z, summit.x, summit.z), tilt);
}

bool writeViews(const terrain::Heightfield& field, const Options& options,
                const terrain::ReliefStats& stats, const std::filesystem::path& directory,
                Report& r) {
    const Point highest = summitNear(field, stats.highestX, stats.highestZ);
    const Point summit =
        options.fixedMountain ? summitNear(field, options.mountainX, options.mountainZ)
        : stats.mountain.found ? summitNear(field, stats.mountain.x, stats.mountain.z)
                               : highest;
    const terrain::ViewParams foot = footView(field, options, summit, r);
    const Point plain = stats.plain.found ? pointAt(field, stats.plain.x, stats.plain.z)
                                          : pointAt(field, stats.coast.x, stats.coast.z);
    const Point valley = lowestAround(field, highest.x, highest.z, {15'000.0});
    const Point over = pointAt(field, foot.eyeX + 0.4 * (summit.x - foot.eyeX),
                               foot.eyeZ + 0.4 * (summit.z - foot.eyeZ));
    r.add("highest summit of the planet: %.0f m at (%.3f, %.3f) km\n",
          static_cast<double>(highest.height), highest.x / 1000.0, highest.z / 1000.0);
    bool written = true;
    if (options.fixedPlain) {
        written = writeView(field, options, "plain_fixed",
                            viewFrom(options, pointAt(field, options.plainX, options.plainZ),
                                     kEyeAboveGround, 0.0, -0.04),
                            directory, r);
    }
    return written && writeView(field, options, "foot", foot, directory, r) &&
           writeView(field, options, "plain", viewFrom(options, plain, kEyeAboveGround, 0.0, -0.04),
                     directory, r) &&
           writeView(field, options, "summit",
                     viewFrom(options, highest, kEyeAboveGround,
                              headingTo(highest.x, highest.z, valley.x, valley.z), -0.12),
                     directory, r) &&
           writeView(field, options, "above",
                     viewFrom(options, over, kFlightAboveGround,
                              headingTo(over.x, over.z, summit.x, summit.z), 0.0),
                     directory, r) &&
           writeView(field, options, "slope", slopeView(field, options, summit), directory, r);
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    std::vector<char*> args;
    if (!parseFlags(argc, argv, options, args) ||
        !parseArguments(static_cast<int>(args.size()), args.data(), options)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [seed] [default|planetary|legacy|geography] "
                     "[width] [cell-size] [threads] [world-x] [world-z] [--plain <x> <z>] "
                     "[--mountain <x> <z>] "
                     "[--without ravines|lively]...\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    const std::filesystem::path directory = args[1];
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    terrain::HeightfieldParams params = reliefParams(options.relief);
    params.geography.ravines = options.ravines;
    params.geography.livelyPlains = options.lively;
    const terrain::Heightfield field(options.seed, params, options.world);
    const terrain::ReliefStats stats = terrain::measureRelief(
        terrain::sampleReliefWorld(field, options.cellSize, options.threads));
    Report r;
    r.add("relief %s, seed %llu, world %.0f x %.0f km, views of %d x %d pixels, 70 degrees "
          "wide\n",
          options.relief.c_str(), static_cast<unsigned long long>(options.seed),
          options.world.x / 1000.0, options.world.z / 1000.0, options.width,
          options.width * 9 / 16);
    if (!writeViews(field, options, stats, directory, r) || !r.write(directory / "views.txt")) {
        std::fprintf(stderr, "Failed to write the views into %s\n", args[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
