// Writes the control images of the far horizon around a viewpoint: level_N.png (every level),
// map_relief.png, map_materials.png (the levels assembled, 1024 px across the coarsest one) and
// panorama.png (360 degrees, 3600 x 480 px, vertical angles x4, curvature of a 6371 km planet).
// Without a position the viewpoint is a summit found with the grids alone: the highest sample
// of the coarsest level around the middle of the map, refined by finer levels around it. The
// eye stands 2 m over the ground. Prints the viewpoint, how far the relief shows, and what the
// lack of a prefilter costs: how the silhouette drawn from the levels differs from the one of a
// finer grid computed by the sampler, within 20 km over 10 degrees (8 m samples) and from 20 km
// to 300 km over 2 degrees (64 m samples).
// Usage: terrain_export_horizon <output-dir> [seed] [x z] [cells-per-side]

#include "terrain/det_math.hpp"
#include "terrain/io/horizon_panorama.hpp"
#include "terrain/io/horizon_png.hpp"
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
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace {

constexpr double kEyeAboveGround = 2.0;
constexpr double kNearDistance = 20'000.0; // meters
constexpr double kFarDistance = 300'000.0;
constexpr int kNearColumns = 100; // 10 degrees of the panorama
constexpr int kFarColumns = 20;
constexpr double kDegreesPerRadian = 57.295779513082320876798154814105;

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    std::filesystem::path directory;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    std::optional<double> x;
    std::optional<double> z;
    int cells = 256;
};

// Reads the arguments. On the first invalid one, prints what is wrong and returns false;
// nothing has been generated or written then.
bool parseArguments(int argc, char** argv, Options& options) {
    if (argc < 2 || argc > 6 || argc == 4) {
        std::fprintf(stderr, "Expected 1, 2, 4 or 5 arguments, got %d\n", argc - 1);
        return false;
    }
    options.directory = argv[1];
    if (argc >= 3 && !parse(argv[2], options.seed)) {
        std::fprintf(stderr, "Invalid seed '%s': expected an unsigned integer\n", argv[2]);
        return false;
    }
    for (int i = 3; i < std::min(argc, 5); ++i) {
        double value = 0.0;
        if (!parse(argv[i], value) || !std::isfinite(value)) {
            std::fprintf(stderr, "Invalid position '%s': expected a number of meters\n", argv[i]);
            return false;
        }
        (i == 3 ? options.x : options.z) = value;
    }
    if (argc == 6 && (!parse(argv[5], options.cells) || options.cells < 16 ||
                      options.cells > 1024 || options.cells % 4 != 0)) {
        std::fprintf(stderr,
                     "Invalid cells per side '%s': expected a multiple of 4 from 16 to "
                     "1024\n",
                     argv[5]);
        return false;
    }
    return true;
}

void settle(terrain::HorizonStreamer& streamer, double x, double z) {
    streamer.update(x, z);
    while (streamer.busy()) {
        streamer.waitIdle();
        streamer.update(x, z);
    }
}

struct Point {
    double x;
    double z;
    float height;
};

// The highest sample of a level, at its image nearest to (x, z).
Point highestSample(const terrain::HorizonLevel& level, double x, double z) {
    Point best{x, z, -1.0e9f};
    for (int k = 0; k < level.side(); ++k) {
        for (int i = 0; i < level.side(); ++i) {
            const float height = level.atWindow(i, k).height;
            if (height > best.height) {
                best = {level.originX(x) + i * level.pitch(), level.originZ(z) + k * level.pitch(),
                        height};
            }
        }
    }
    return best;
}

// A summit found with the grids: the highest sample of the coarsest level around the start,
// then of finer and finer levels around it.
Point findSummit(terrain::HorizonStreamer& streamer, double x, double z) {
    const int levels = streamer.clipmap().levelCount();
    Point summit{x, z, 0.0f};
    for (const int level : {levels - 1, levels * 2 / 3, levels / 3, 0}) {
        settle(streamer, summit.x, summit.z);
        summit = highestSample(streamer.level(level), summit.x, summit.z);
    }
    settle(streamer, summit.x, summit.z);
    return summit;
}

// A sector of the panorama whose silhouette is compared with the one of a finer grid: its
// columns, the distances of the ground it covers, and the level whose pitch the grid has.
struct Sector {
    int first = 0;
    int columns = 0;
    double minDistance = 0.0;
    double maxDistance = 0.0;
    int level = 0;
};

// Samples of a level over a rectangle, computed by the sampler on every hardware thread, read
// as a ground (bilinear).
struct ReferenceGrid {
    terrain::HorizonRect rect;
    int pitch = 1;
    std::vector<terrain::HorizonCell> cells;

    [[nodiscard]] std::optional<float> heightAt(double x, double z) const {
        const double u = x / pitch - static_cast<double>(rect.x0);
        const double v = z / pitch - static_cast<double>(rect.z0);
        const auto lastU = static_cast<double>(rect.width() - 1);
        const auto lastV = static_cast<double>(rect.height() - 1);
        if (u < 0.0 || v < 0.0 || u >= lastU || v >= lastV) {
            return std::nullopt;
        }
        const auto i = static_cast<std::int64_t>(u);
        const auto k = static_cast<std::int64_t>(v);
        const double s = u - static_cast<double>(i);
        const double t = v - static_cast<double>(k);
        const auto h = [&](std::int64_t a, std::int64_t b) {
            return static_cast<double>(
                cells[static_cast<std::size_t>(b * rect.width() + a)].height);
        };
        const double top = h(i, k) * (1.0 - s) + h(i + 1, k) * s;
        const double bottom = h(i, k + 1) * (1.0 - s) + h(i + 1, k + 1) * s;
        return static_cast<float>(top * (1.0 - t) + bottom * t);
    }
};

ReferenceGrid referenceGrid(const terrain::HorizonSampler& sampler, int level,
                            const terrain::HorizonRect& rect) {
    ReferenceGrid grid{rect, terrain::levelPitch(sampler.params(), level), {}};
    grid.cells.resize(static_cast<std::size_t>(rect.count()));
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    const std::int64_t rows = (rect.height() + threads - 1) / threads;
    std::vector<std::jthread> workers;
    for (int t = 0; t < threads; ++t) {
        const std::int64_t z0 = rect.z0 + t * rows;
        const std::int64_t z1 = std::min(rect.z1, z0 + rows);
        if (z0 >= z1) {
            break;
        }
        workers.emplace_back([&sampler, &grid, level, rect, z0, z1] {
            const terrain::HorizonRect part{rect.x0, z0, rect.x1, z1};
            const auto first = static_cast<std::size_t>((z0 - rect.z0) * rect.width());
            const auto count = static_cast<std::size_t>(part.count());
            sampler.fill(level, part, std::span(grid.cells).subspan(first, count));
        });
    }
    workers.clear(); // joined before the grid is returned
    return grid;
}

// Rectangle of samples of the given pitch holding the ground of the sector.
terrain::HorizonRect sectorRect(const terrain::PanoramaParams& params, const Sector& sector,
                                int pitch) {
    double minX = params.eyeX;
    double maxX = params.eyeX;
    double minZ = params.eyeZ;
    double maxZ = params.eyeZ;
    for (int c = 0; c <= sector.columns; ++c) {
        const double azimuth = terrain::columnAzimuth(params, sector.first + c);
        for (const double d : {sector.minDistance, sector.maxDistance}) {
            const double x = params.eyeX + d * terrain::dmath::sinTurns(azimuth);
            const double z = params.eyeZ - d * terrain::dmath::cosTurns(azimuth);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    }
    return {static_cast<std::int64_t>(std::floor(minX / pitch)) - 4,
            static_cast<std::int64_t>(std::floor(minZ / pitch)) - 4,
            static_cast<std::int64_t>(std::ceil(maxX / pitch)) + 5,
            static_cast<std::int64_t>(std::ceil(maxZ / pitch)) + 5};
}

// The silhouette of the ground of a sector, from the levels of the clipmap.
terrain::Silhouette levelSilhouette(const terrain::HorizonClipmap& clipmap,
                                    terrain::PanoramaParams params, const Sector& sector) {
    params.minDistance = sector.minDistance;
    return terrain::traceSilhouette(terrain::clipmapGround(clipmap), params, sector.first,
                                    sector.columns, sector.maxDistance);
}

double meanTangent(const terrain::Silhouette& silhouette) {
    double sum = 0.0;
    for (const double tangent : silhouette.tangent) {
        sum += tangent / static_cast<double>(silhouette.tangent.size());
    }
    return sum;
}

struct SilhouetteLoss {
    double azimuthDegrees = 0.0;
    double meanDegrees = 0.0; // mean of (levels - grid): negative when the levels are lower
    double meanAbsDegrees = 0.0;
    double maxAbsDegrees = 0.0;
};

// Elevation angles of the silhouette of the sector from the levels minus those from a grid of
// the pitch of sector.level, computed by the sampler.
SilhouetteLoss sectorLoss(const terrain::HorizonSampler& sampler,
                          const terrain::HorizonClipmap& clipmap,
                          const terrain::PanoramaParams& params, const Sector& sector) {
    const terrain::Silhouette levels = levelSilhouette(clipmap, params, sector);
    const int pitch = terrain::levelPitch(sampler.params(), sector.level);
    const ReferenceGrid grid =
        referenceGrid(sampler, sector.level, sectorRect(params, sector, pitch));
    terrain::PanoramaParams ranged = params;
    ranged.minDistance = sector.minDistance;
    const terrain::Silhouette fine =
        terrain::traceSilhouette([&grid](double x, double z) { return grid.heightAt(x, z); },
                                 ranged, sector.first, sector.columns, sector.maxDistance);
    SilhouetteLoss loss;
    const double center = terrain::columnAzimuth(params, sector.first + sector.columns / 2);
    loss.azimuthDegrees = 360.0 * (center - std::floor(center));
    const auto columns = static_cast<double>(sector.columns);
    for (std::size_t c = 0; c < levels.tangent.size(); ++c) {
        const double difference =
            (terrain::dmath::atan(levels.tangent[c]) - terrain::dmath::atan(fine.tangent[c])) *
            kDegreesPerRadian;
        loss.meanDegrees += difference / columns;
        loss.meanAbsDegrees += std::abs(difference) / columns;
        loss.maxAbsDegrees = std::max(loss.maxAbsDegrees, std::abs(difference));
    }
    return loss;
}

// The near sector: 10 degrees around the highest point of the panorama within 20 km, against
// samples of level 0.
Sector nearSector(const terrain::HorizonClipmap& clipmap, const terrain::PanoramaParams& params) {
    const Sector all{0, params.width, params.minDistance, kNearDistance, 0};
    const terrain::Silhouette levels = levelSilhouette(clipmap, params, all);
    const auto highest = std::max_element(levels.tangent.begin(), levels.tangent.end());
    const int center = static_cast<int>(highest - levels.tangent.begin());
    const int first = std::clamp(center - kNearColumns / 2, 0, params.width - kNearColumns);
    return {first, kNearColumns, params.minDistance, kNearDistance, 0};
}

// The far sector: 2 degrees around the north, the east, the south or the west (a narrow grid),
// whichever shows the highest relief between 20 km and 300 km (or the reach of the levels),
// against samples of level 3 (64 m by default).
Sector farSector(const terrain::HorizonClipmap& clipmap, const terrain::PanoramaParams& params) {
    const double reach = std::min(kFarDistance, terrain::guaranteedRadius(clipmap.params()));
    const int level = std::min(3, clipmap.levelCount() - 1);
    Sector best{0, kFarColumns, kNearDistance, reach, level};
    double highest = -1.0e9;
    for (int quarter = 0; quarter < 4; ++quarter) {
        const Sector sector{quarter * params.width / 4 - kFarColumns / 2, kFarColumns,
                            kNearDistance, reach, level};
        const double tangent = meanTangent(levelSilhouette(clipmap, params, sector));
        if (tangent > highest) {
            highest = tangent;
            best = sector;
        }
    }
    return best;
}

void printLoss(const char* name, const Sector& sector, int pitch, const SilhouetteLoss& loss) {
    std::printf("silhouette %s, %.0f to %.0f km, %.0f degrees around azimuth %.1f, levels - %d m "
                "samples: mean %+.3f deg, mean |diff| %.3f deg, max |diff| %.3f deg\n",
                name, sector.minDistance / 1000.0, sector.maxDistance / 1000.0,
                sector.columns / 10.0, loss.azimuthDegrees, pitch, loss.meanDegrees,
                loss.meanAbsDegrees, loss.maxAbsDegrees);
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0.0 : values[values.size() / 2];
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr, "Usage: terrain_export_horizon <output-dir> [seed] [x z] "
                             "[cells-per-side]\n");
        return EXIT_FAILURE;
    }
    const auto start = std::chrono::steady_clock::now();
    const terrain::Heightfield field(options.seed);
    const terrain::Ecology ecology(options.seed);
    terrain::HorizonParams params;
    params.cellsPerSide = options.cells;
    const terrain::HorizonSampler sampler(field, ecology, params);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    terrain::HorizonStreamer streamer(sampler, {threads, terrain::kHorizonPieceSamples});
    Point eye{options.x.value_or(0.0), options.z.value_or(0.0), 0.0f};
    if (options.x) {
        settle(streamer, eye.x, eye.z);
        eye.height = terrain::horizonPointAt(streamer.clipmap(), eye.x, eye.z).value().height;
    } else {
        eye = findSummit(streamer, 0.0, 0.0);
    }
    terrain::PanoramaParams panoramaParams;
    panoramaParams.eyeX = eye.x;
    panoramaParams.eyeZ = eye.z;
    panoramaParams.eyeHeight = eye.height + kEyeAboveGround;
    const terrain::Panorama panorama =
        terrain::renderHorizonPanorama(streamer.clipmap(), panoramaParams);
    if (!terrain::exportHorizonMaps(streamer.clipmap(), eye.x, eye.z, options.directory) ||
        !terrain::writePng(options.directory / "panorama.png", panorama.image)) {
        std::fprintf(stderr, "Cannot write the images into %s\n",
                     options.directory.string().c_str());
        return EXIT_FAILURE;
    }
    const Sector nearby = nearSector(streamer.clipmap(), panoramaParams);
    const Sector distant = farSector(streamer.clipmap(), panoramaParams);
    const SilhouetteLoss nearLoss = sectorLoss(sampler, streamer.clipmap(), panoramaParams, nearby);
    const SilhouetteLoss farLoss = sectorLoss(sampler, streamer.clipmap(), panoramaParams, distant);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("seed %llu, %d levels of %d x %d samples, %d m to %d m, radius %.0f km\n",
                static_cast<unsigned long long>(options.seed), params.levels,
                terrain::samplesPerSide(params), terrain::samplesPerSide(params),
                terrain::levelPitch(params, 0), terrain::levelPitch(params, params.levels - 1),
                terrain::guaranteedRadius(params) / 1000.0);
    std::printf("viewpoint (%.0f, %.0f), ground %.1f m, eye %.1f m: relief shows up to %.0f km "
                "(median over the azimuths %.0f km)\n",
                eye.x, eye.z, eye.height, panoramaParams.eyeHeight,
                *std::max_element(panorama.farthest.begin(), panorama.farthest.end()) / 1000.0,
                median(panorama.farthest) / 1000.0);
    printLoss("near", nearby, terrain::levelPitch(params, nearby.level), nearLoss);
    printLoss("far", distant, terrain::levelPitch(params, distant.level), farLoss);
    std::printf("written into %s in %.1f s\n", options.directory.string().c_str(), seconds);
    return EXIT_SUCCESS;
}
