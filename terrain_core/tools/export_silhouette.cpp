// Measures the silhouette of the far horizon against the true one at two places of the default
// world, the eye 2 m over the ground: the floor of the valley of the K2 (416,857, -11,398,966 m,
// heading 0) and the foot of the longest chain (9,618,717, -11,025,212 m, heading 67.5 degrees).
// Over 120 degrees around the heading: the truth is Heightfield::heightAt along the ray of each
// column, in steps of 2 m or distance / 2048, out to the reach of the levels; before, the levels
// of point samples (crestRatio 0); after, the levels with the crests of each ratio given (the
// default ratio without any), traced in the same steps. Writes silhouette_<place>_r<R>.png
// (truth | before | after, then their differences: before orange, after blue, +-1 degree),
// zoom_<place>_r<R>.png (the same over 20 degrees around the highest point of the truth, 3
// times as many columns per degree, +-0.5 degree) and the control panoramas
// panorama_<place>_before.png and panorama_<place>_r<R>.png. Prints the differences in degrees
// (all the columns, and by distance of the true silhouette), the lifts of the samples per level
// (land, valley floors: lower than both neighbors along an axis, and under the eye) and, with
// --timings, the time of a full epoch and of the updates of a walk of 2 km along the heading in
// steps of 8 m, on 1 and 2 worker threads, before and for each ratio, and the time of the
// heights the crests read on the sides and at the centers of their cells (heightsAt).
// Usage: terrain_export_silhouette <output-dir> [--timings] [cells-per-side]
//        [columns-per-degree] [ratio...]

#include "terrain/det_math.hpp"
#include "terrain/io/horizon_panorama.hpp"
#include "terrain/io/horizon_png.hpp"
#include "terrain/io/horizon_silhouette.hpp"
#include "terrain/streaming/horizon_streamer.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kEyeAboveGround = 2.0;
constexpr double kSectorDegrees = 120.0;
constexpr double kTruthDivisor = 2048.0;

struct Place {
    const char* name;
    double x;
    double z;
    double headingDegrees;
};

constexpr Place kPlaces[] = {
    {"k2_valley", 416'857.0, -11'398'966.0, 0.0},
    {"chain_foot", 9'618'717.0, -11'025'212.0, 67.5},
};

struct Options {
    std::filesystem::path directory;
    bool timings = false;
    int cells = 256;
    int columnsPerDegree = 10;
    std::vector<int> ratios;
};

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

// Reads the arguments. On the first invalid one, prints what is wrong and returns false;
// nothing has been generated or written then.
bool parseArguments(int argc, char** argv, Options& options) {
    if (argc < 2) {
        std::fprintf(stderr, "Expected an output directory\n");
        return false;
    }
    options.directory = argv[1];
    int next = 2;
    if (next < argc && std::strcmp(argv[next], "--timings") == 0) {
        options.timings = true;
        ++next;
    }
    if (next < argc && (!parse(argv[next], options.cells) || options.cells < 16 ||
                        options.cells > 1024 || options.cells % 4 != 0)) {
        std::fprintf(stderr, "Invalid cells per side '%s': expected a multiple of 4 from 16 to "
                             "1024\n", argv[next]);
        return false;
    }
    ++next;
    if (next < argc && (!parse(argv[next], options.columnsPerDegree) ||
                        options.columnsPerDegree < 1 || options.columnsPerDegree > 20)) {
        std::fprintf(stderr, "Invalid columns per degree '%s': expected 1 to 20\n", argv[next]);
        return false;
    }
    for (++next; next < argc; ++next) {
        int ratio = 0;
        if (!parse(argv[next], ratio) || ratio < 2 || ratio > terrain::kHorizonCrestRatioMax ||
            (ratio & (ratio - 1)) != 0) {
            std::fprintf(stderr, "Invalid crest ratio '%s': expected 2, 4, 8 or 16\n",
                         argv[next]);
            return false;
        }
        options.ratios.push_back(ratio);
    }
    if (options.ratios.empty()) {
        options.ratios.push_back(terrain::HorizonParams{}.crestRatio);
    }
    return true;
}

int hardwareThreads() { return std::max(1, static_cast<int>(std::thread::hardware_concurrency())); }

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

terrain::HorizonParams horizonParams(int cells, int ratio) {
    terrain::HorizonParams params;
    params.cellsPerSide = cells;
    params.crestRatio = ratio;
    return params;
}

void settle(terrain::HorizonStreamer& streamer, double x, double z) {
    streamer.update(x, z);
    while (streamer.busy()) {
        streamer.waitIdle();
        streamer.update(x, z);
    }
}

// The levels around the eye, computed on every hardware thread.
struct Levels {
    terrain::HorizonSampler sampler;
    terrain::HorizonStreamer streamer;

    Levels(const terrain::Heightfield& field, const terrain::Ecology& ecology,
           const terrain::HorizonParams& params, const Place& place)
        : sampler(field, ecology, params),
          streamer(sampler, {hardwareThreads(), terrain::kHorizonPieceSamples}) {
        settle(streamer, place.x, place.z);
    }
};

// The columns of the sector around the heading.
struct Sector {
    int first = 0;
    int columns = 0;
    double maxDistance = 0.0;
};

// The silhouette of the sector over a ground, its columns traced on every hardware thread.
terrain::Silhouette traceSector(const terrain::GroundHeight& ground,
                                const terrain::PanoramaParams& params, const Sector& sector,
                                double divisor) {
    const int threads = hardwareThreads();
    const int block = (sector.columns + threads - 1) / threads;
    std::vector<terrain::Silhouette> parts(static_cast<std::size_t>(threads));
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < threads; ++t) {
            const int first = t * block;
            const int count = std::max(0, std::min(block, sector.columns - first));
            workers.emplace_back([&, t, first, count] {
                parts[static_cast<std::size_t>(t)] = terrain::traceSilhouette(
                    ground, params, sector.first + first, count, sector.maxDistance, divisor);
            });
        }
    }
    terrain::Silhouette whole;
    for (const terrain::Silhouette& part : parts) {
        whole.tangent.insert(whole.tangent.end(), part.tangent.begin(), part.tangent.end());
        whole.distance.insert(whole.distance.end(), part.distance.begin(), part.distance.end());
    }
    return whole;
}

void printError(const char* what, const terrain::Silhouette& levels,
                const terrain::Silhouette& truth) {
    const terrain::SilhouetteError all = terrain::silhouetteError(levels, truth);
    std::printf("  %-7s mean %+.3f deg, mean |diff| %.3f, p95 %.3f, max %.3f;", what,
                all.meanDegrees, all.meanAbsDegrees, all.p95AbsDegrees, all.maxAbsDegrees);
    constexpr double kBands[] = {0.0, 2'000.0, 10'000.0, 50'000.0, 150'000.0, 1.0e9};
    for (std::size_t b = 0; b + 1 < std::size(kBands); ++b) {
        const terrain::SilhouetteError band =
            terrain::silhouetteError(levels, truth, kBands[b], kBands[b + 1]);
        if (band.columns > 0) {
            std::printf(" %.0f-%.0f km: %+.3f / %.3f (%d)", kBands[b] / 1000.0,
                        std::min(kBands[b + 1], 1.0e6) / 1000.0, band.meanDegrees,
                        band.meanAbsDegrees, band.columns);
        }
    }
    std::printf("\n");
}

// Lifts of the samples of one level: on land, on the valley floors (lower than both neighbors
// along x or along z), and under the eye.
void printLifts(const terrain::HorizonLevel& points, const terrain::HorizonLevel& crests,
                const Place& place) {
    int land = 0;
    int lifted = 0;
    int floors = 0;
    int floorsLifted = 0;
    double sum = 0.0;
    double largest = 0.0;
    double floorSum = 0.0;
    double floorLargest = 0.0;
    const auto p = [&](int i, int k) { return points.atWindow(i, k).height; };
    for (int k = 1; k + 1 < points.side(); ++k) {
        for (int i = 1; i + 1 < points.side(); ++i) {
            if (p(i, k) < terrain::kSeaLevel) {
                continue;
            }
            const double lift = crests.atWindow(i, k).height - p(i, k);
            ++land;
            lifted += lift > 0.0 ? 1 : 0;
            sum += lift;
            largest = std::max(largest, lift);
            if ((p(i, k) < p(i - 1, k) && p(i, k) < p(i + 1, k)) ||
                (p(i, k) < p(i, k - 1) && p(i, k) < p(i, k + 1))) {
                ++floors;
                floorsLifted += lift > 0.0 ? 1 : 0;
                floorSum += lift;
                floorLargest = std::max(floorLargest, lift);
            }
        }
    }
    const double eye = *crests.heightAt(place.x, place.z) - *points.heightAt(place.x, place.z);
    std::printf("  level %d (%d m): land %d, lifted %.1f %%, mean %.1f m, max %.0f m; valley "
                "floors %d, lifted %.1f %%, mean %.2f m, max %.0f m; under the eye %+.2f m\n",
                points.level(), points.pitch(), land, 100.0 * lifted / std::max(land, 1),
                sum / std::max(land, 1), largest, floors,
                100.0 * floorsLifted / std::max(floors, 1),
                floorSum / std::max(floors, 1), floorLargest, eye);
}

// The time of a full epoch around the place and of the updates of a walk of 2 km along the
// heading in steps of 8 m, on `threads` workers.
void printTimings(const terrain::Heightfield& field, const terrain::Ecology& ecology,
                  const terrain::HorizonParams& params, const Place& place, int threads) {
    const terrain::HorizonSampler sampler(field, ecology, params);
    terrain::HorizonStreamer streamer(sampler, {threads, terrain::kHorizonPieceSamples});
    const auto start = std::chrono::steady_clock::now();
    settle(streamer, place.x, place.z);
    const double epoch = secondsSince(start);
    const double turns = place.headingDegrees / 360.0;
    const double dx = terrain::dmath::sinTurns(turns);
    const double dz = -terrain::dmath::cosTurns(turns);
    double total = 0.0;
    double worst = 0.0;
    constexpr int kSteps = 250;
    for (int step = 1; step <= kSteps; ++step) {
        const auto before = std::chrono::steady_clock::now();
        settle(streamer, place.x + 8.0 * step * dx, place.z + 8.0 * step * dz);
        const double seconds = secondsSince(before);
        total += seconds;
        worst = std::max(worst, seconds);
    }
    std::printf("  ratio %d, %d thread%s: epoch %.2f s, walk %.2f ms per step (max %.1f ms), "
                "%.1f MB\n",
                params.crestRatio, threads, threads == 1 ? "" : "s", epoch, 1e3 * total / kSteps,
                1e3 * worst, static_cast<double>(streamer.clipmap().memoryBytes()) / 1e6);
}

// The time of heightsAt on the points the crests read in every window of the levels 1 and up
// around the place (every cell taken as examined: high mountains).
double latticeSeconds(const terrain::Heightfield& field, const terrain::HorizonParams& params,
                      const Place& place) {
    const int ratio = params.crestRatio;
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<float> out;
    const auto start = std::chrono::steady_clock::now();
    for (int level = 1; level < params.levels; ++level) {
        const terrain::HorizonRect window = terrain::windowAround(
            terrain::canonicalCenter(params, level, place.x, place.z), params.cellsPerSide);
        const terrain::HorizonIndex counts = terrain::levelCounts(params, level);
        const int spacing = terrain::levelPitch(params, level) / ratio;
        for (std::int64_t v = (window.z0 - 1) * ratio; v <= window.z1 * ratio; ++v) {
            xs.clear();
            for (std::int64_t u = (window.x0 - 1) * ratio; u <= window.x1 * ratio; ++u) {
                const std::int64_t a = terrain::wrapIndex(u, ratio);
                const std::int64_t b = terrain::wrapIndex(v, ratio);
                if ((a == 0) != (b == 0) || (2 * a == ratio && 2 * b == ratio)) {
                    xs.push_back(terrain::samplePosition(terrain::wrapIndex(u, counts.x * ratio),
                                                         spacing));
                }
            }
            zs.assign(xs.size(), terrain::samplePosition(
                                     terrain::wrapIndex(v, counts.z * ratio), spacing));
            out.resize(xs.size());
            field.heightsAt(xs, zs, out);
        }
    }
    return secondsSince(start);
}

terrain::PanoramaParams eyeAt(const terrain::Heightfield& field, const Place& place) {
    terrain::PanoramaParams params;
    params.eyeX = place.x;
    params.eyeZ = place.z;
    params.eyeHeight = field.heightAt(place.x, place.z) + kEyeAboveGround;
    return params;
}

bool writeImage(const std::filesystem::path& path, const terrain::Image& image) {
    if (!terrain::writePng(path, image)) {
        std::fprintf(stderr, "Cannot write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

// A sector of the panorama from the eye: its columns at a number per degree.
struct View {
    terrain::PanoramaParams panorama;
    Sector sector;
};

View viewOf(const terrain::PanoramaParams& eye, int columnsPerDegree, double centerDegrees,
            double widthDegrees, double maxDistance) {
    View view{eye, {}};
    view.panorama.width = 360 * columnsPerDegree;
    view.sector = {static_cast<int>(std::lround((centerDegrees - widthDegrees / 2.0) *
                                                columnsPerDegree)),
                   static_cast<int>(std::lround(widthDegrees * columnsPerDegree)), maxDistance};
    return view;
}

terrain::Silhouette traceTruth(const terrain::Heightfield& field, const View& view,
                               double divisor = kTruthDivisor) {
    const terrain::GroundHeight ground = [&field](double x, double z) {
        return std::optional<float>(field.heightAt(x, z));
    };
    return traceSector(ground, view.panorama, view.sector, divisor);
}

terrain::Silhouette traceLevels(const Levels& levels, const View& view) {
    return traceSector(terrain::clipmapGround(levels.streamer.clipmap()), view.panorama,
                       view.sector, kTruthDivisor);
}

// The largest change of the truth on a tenth of its columns with steps twice as fine.
void printTruthCheck(const terrain::Heightfield& field, const View& view,
                     const terrain::Silhouette& truth, double seconds) {
    View sparse = view;
    sparse.sector.columns /= 10;
    const terrain::Silhouette finer = traceTruth(field, sparse, 2 * kTruthDivisor);
    double drift = 0.0;
    for (std::size_t c = 0; c < finer.tangent.size(); ++c) {
        drift = std::max(drift, std::abs(terrain::silhouetteDegrees(finer.tangent[c]) -
                                         terrain::silhouetteDegrees(truth.tangent[c])));
    }
    std::printf("  truth in %.1f s; against steps of d / %.0f on its first %d columns: max "
                "%.4f deg\n",
                seconds, 2 * kTruthDivisor, sparse.sector.columns, drift);
}

// The view of 20 degrees around the highest column of the truth, 3 times as many columns per
// degree.
View zoomOf(const View& view, const terrain::Silhouette& truth) {
    const auto highest = static_cast<int>(
        std::max_element(truth.tangent.begin(), truth.tangent.end()) - truth.tangent.begin());
    const double degrees =
        360.0 * terrain::columnAzimuth(view.panorama, view.sector.first + highest);
    return viewOf(view.panorama, 3 * view.panorama.width / 360, degrees, 20.0,
                  view.sector.maxDistance);
}

// Measures one place; returns false if an image cannot be written.
bool measurePlace(const Options& options, const terrain::Heightfield& field,
                  const terrain::Ecology& ecology, const Place& place) {
    const terrain::HorizonParams before = horizonParams(options.cells, 0);
    const View view = viewOf(eyeAt(field, place), options.columnsPerDegree, place.headingDegrees,
                             kSectorDegrees, terrain::guaranteedRadius(before));
    std::printf("%s: (%.0f, %.0f), ground %.1f m, heading %.1f deg, %d columns to %.0f km\n",
                place.name, place.x, place.z, view.panorama.eyeHeight - kEyeAboveGround,
                place.headingDegrees, view.sector.columns, view.sector.maxDistance / 1000.0);
    const auto start = std::chrono::steady_clock::now();
    const terrain::Silhouette truth = traceTruth(field, view);
    printTruthCheck(field, view, truth, secondsSince(start));
    const View zoom = zoomOf(view, truth);
    const terrain::Silhouette zoomTruth = traceTruth(field, zoom);
    const Levels points(field, ecology, before, place);
    const terrain::Silhouette flat = traceLevels(points, view);
    const terrain::Silhouette zoomFlat = traceLevels(points, zoom);
    printError("before", flat, truth);
    const std::filesystem::path& dir = options.directory;
    const std::string stem = std::string(place.name);
    // 3600 columns at 10 per degree: the control panorama.
    bool written =
        writeImage(dir / ("panorama_" + stem + "_before.png"),
                   terrain::renderHorizonPanorama(points.streamer.clipmap(), view.panorama).image);
    for (const int ratio : options.ratios) {
        const Levels crests(field, ecology, horizonParams(options.cells, ratio), place);
        const terrain::Silhouette raised = traceLevels(crests, view);
        std::printf(" ratio %d\n", ratio);
        printError("after", raised, truth);
        for (int n = 1; n < before.levels; ++n) {
            printLifts(points.streamer.level(n), crests.streamer.level(n), place);
        }
        const std::string suffix = "_r" + std::to_string(ratio) + ".png";
        written = written &&
                  writeImage(dir / ("silhouette_" + stem + suffix),
                             terrain::renderSilhouettes({truth, flat, raised})) &&
                  writeImage(dir / ("zoom_" + stem + suffix),
                             terrain::renderSilhouettes(
                                 {zoomTruth, zoomFlat, traceLevels(crests, zoom)}, 300, 0.5)) &&
                  writeImage(dir / ("panorama_" + stem + suffix),
                             terrain::renderHorizonPanorama(crests.streamer.clipmap(),
                                                            view.panorama)
                                 .image);
    }
    return written;
}

void measureTimings(const Options& options, const terrain::Heightfield& field,
                    const terrain::Ecology& ecology, const Place& place) {
    std::printf("%s: timings, K = %d\n", place.name, options.cells);
    std::vector<int> ratios{0};
    ratios.insert(ratios.end(), options.ratios.begin(), options.ratios.end());
    for (const int threads : {1, 2}) {
        for (const int ratio : ratios) {
            printTimings(field, ecology, horizonParams(options.cells, ratio), place, threads);
        }
    }
    for (const int ratio : options.ratios) {
        std::printf("  ratio %d: heightsAt of the points of the cells of the levels 1 and up, 1 "
                    "thread: %.2f s\n",
                    ratio, latticeSeconds(field, horizonParams(options.cells, ratio), place));
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr, "Usage: terrain_export_silhouette <output-dir> [--timings] "
                             "[cells-per-side] [columns-per-degree] [ratio...]\n");
        return EXIT_FAILURE;
    }
    std::error_code error;
    std::filesystem::create_directories(options.directory, error);
    if (error) {
        std::fprintf(stderr, "Cannot create %s\n", options.directory.string().c_str());
        return EXIT_FAILURE;
    }
    const auto start = std::chrono::steady_clock::now();
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const terrain::Ecology ecology(terrain::kDefaultWorldSeed);
    for (const Place& place : kPlaces) {
        if (!measurePlace(options, field, ecology, place)) {
            return EXIT_FAILURE;
        }
    }
    if (options.timings) {
        for (const Place& place : kPlaces) {
            measureTimings(options, field, ecology, place);
        }
    }
    std::printf("written into %s in %.1f s\n", options.directory.string().c_str(),
                secondsSince(start));
    return EXIT_SUCCESS;
}
