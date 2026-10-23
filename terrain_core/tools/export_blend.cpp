// Maps of the transitions between the biomes and the materials (biome_blend.hpp) on the streamed
// world: the surface of the Heightfield, as the tiles have it (no river).
// - maps: around each place given by --at, at 200, 20 and 2 km (size x size pixels), the biomes
//   and the materials before (the dominant one alone, the classification) and after (the colors
//   mixed by the weights): <name>_<km>km_<biomes|materials>_<before|after>.png; prints the share
//   of the samples within a band (a second weight of at least 5 %).
// - edge: the edge between sand and rock nearest to (x, z) (sand within 16 m of rock, searched
//   every 16 m within 3 km; the shores of the streamed world are gentle, the sand meets the rock
//   in the deserts at the foot of steep ground), a window of 128 m around it: edge_before.png,
//   the dominant material every 0.5 m on the site of the LOD 0 tiles (slopes over 1 m),
//   edge_reference.png, the materials of the blend there, and edge_texels.png, the texels of the
//   LOD 0 tiles (every 4 m) blended between the 4 around each pixel as a material would; prints the
//   mean width of the band between sand and rock (rock weight from 5 % to 95 %) in both.
// Without --seed: the default world seed. Places: those of terrain_export_view (views.txt).
// Usage: terrain_export_blend maps <output-dir> [--seed <s>] [--size <px>] [--threads <n>]
//                             --at <name> <x> <z> [--at <name> <x> <z>]...
//        terrain_export_blend edge <output-dir> <x> <z> [--seed <s>]

#include "terrain/chunk_coord.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/io/blend_png.hpp"
#include "terrain/io/png.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::BlendWeights;
using terrain::SurfaceBlend;

constexpr int kBlock = 64; // pixels per side of the blocks computed by the threads
constexpr double kEdgeWindow = 128.0;
constexpr double kEdgePixel = 0.5;
constexpr double kEdgeSearch = 3000.0;
constexpr double kSearchStep = 16.0;

template <class T>
bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
};

struct Options {
    std::string mode;
    std::filesystem::path directory;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    int size = 1024;
    int threads = 0; // all the hardware threads
    std::vector<Place> places;
    double x = 0.0; // edge
    double z = 0.0;
};

bool parseFlag(int argc, char** argv, int& k, Options& o) {
    const char* flag = argv[k];
    if (std::strcmp(flag, "--seed") == 0 && k + 1 < argc) {
        return parse(argv[++k], o.seed);
    }
    if (std::strcmp(flag, "--size") == 0 && k + 1 < argc) {
        return parse(argv[++k], o.size) && o.size >= 16;
    }
    if (std::strcmp(flag, "--threads") == 0 && k + 1 < argc) {
        return parse(argv[++k], o.threads) && o.threads >= 1;
    }
    if (std::strcmp(flag, "--at") == 0 && k + 3 < argc && o.mode == "maps") {
        Place place{argv[k + 1], 0.0, 0.0};
        k += 3;
        o.places.push_back(place);
        return !place.name.empty() && parse(argv[k - 1], o.places.back().x) &&
               parse(argv[k], o.places.back().z) && std::isfinite(o.places.back().x) &&
               std::isfinite(o.places.back().z);
    }
    return false;
}

bool parseArguments(int argc, char** argv, Options& o) {
    if (argc < 3) {
        return false;
    }
    o.mode = argv[1];
    o.directory = argv[2];
    int k = 3;
    if (o.mode == "edge") {
        if (argc < 5 || !parse(argv[3], o.x) || !parse(argv[4], o.z) || !std::isfinite(o.x) ||
            !std::isfinite(o.z)) {
            return false;
        }
        k = 5;
    } else if (o.mode != "maps") {
        return false;
    }
    for (; k < argc; ++k) {
        if (!parseFlag(argc, argv, k, o)) {
            return false;
        }
    }
    return o.mode == "edge" || !o.places.empty();
}

// The blends of a square of size x size samples (row by row, z growing downward) centered on
// (centerX, centerZ), in square blocks of kBlock samples computed by the threads: each block is
// the surface of the Heightfield of its region (surfaceFromHeightfield), so the result never
// depends on the threads.
std::vector<SurfaceBlend> blendSquare(const terrain::Heightfield& field,
                                      const terrain::Ecology& ecology, double centerX,
                                      double centerZ, double metersPerPixel, int size,
                                      int threads) {
    const int block = size % kBlock == 0 ? kBlock : size;
    const int blocks = size / block;
    std::vector<SurfaceBlend> result(static_cast<std::size_t>(size) *
                                     static_cast<std::size_t>(size));
    std::atomic<int> next{0};
    const auto work = [&] {
        for (int b = next++; b < blocks * blocks; b = next++) {
            const int bx = b % blocks;
            const int bz = b / blocks;
            const double half = block / 2.0 - size / 2.0;
            const terrain::HeightmapRegion region{centerX + (bx * block + half) * metersPerPixel,
                                                  centerZ + (bz * block + half) * metersPerPixel,
                                                  metersPerPixel, block};
            const terrain::TerrainSurface surface = terrain::surfaceFromHeightfield(field, region);
            const std::vector<SurfaceBlend> blends =
                ecology.blendMap(surface, ecology.map(surface));
            for (int row = 0; row < block; ++row) {
                const auto at =
                    static_cast<std::size_t>(bz * block + row) * static_cast<std::size_t>(size) +
                    static_cast<std::size_t>(bx * block);
                std::copy_n(blends.begin() + static_cast<std::ptrdiff_t>(row * block), block,
                            result.begin() + static_cast<std::ptrdiff_t>(at));
            }
        }
    };
    std::vector<std::thread> workers;
    for (int t = 1; t < threads; ++t) {
        workers.emplace_back(work);
    }
    work();
    for (std::thread& worker : workers) {
        worker.join();
    }
    return result;
}

bool writeMaps(const Options& o, const std::string& stem, const std::vector<SurfaceBlend>& blends) {
    std::vector<BlendWeights> before;
    std::vector<BlendWeights> after;
    before.reserve(blends.size());
    after.reserve(blends.size());
    std::size_t biomeBand = 0;
    std::size_t materialBand = 0;
    for (const SurfaceBlend& blend : blends) {
        before.push_back(terrain::dominantWeights(blend));
        after.push_back(terrain::blendWeights(blend));
        biomeBand += blend.biomeWeights[1] >= 0.05f ? 1 : 0;
        materialBand += blend.materialWeights[1] >= 0.05f ? 1 : 0;
    }
    const double count = static_cast<double>(blends.size());
    std::printf("%s: %.1f %% of the samples in a band of biomes, %.1f %% of materials\n",
                stem.c_str(), 100.0 * static_cast<double>(biomeBand) / count,
                100.0 * static_cast<double>(materialBand) / count);
    const auto path = [&](const char* what) { return o.directory / (stem + "_" + what + ".png"); };
    return terrain::writePng(path("biomes_before"),
                             terrain::renderMixedBiomeMap(before, o.size, o.size)) &&
           terrain::writePng(path("biomes_after"),
                             terrain::renderMixedBiomeMap(after, o.size, o.size)) &&
           terrain::writePng(path("materials_before"),
                             terrain::renderMixedMaterialMap(before, o.size, o.size)) &&
           terrain::writePng(path("materials_after"),
                             terrain::renderMixedMaterialMap(after, o.size, o.size));
}

int runMaps(const Options& o) {
    const terrain::Heightfield field(o.seed);
    const terrain::Ecology ecology(o.seed);
    const int threads = o.threads > 0
                            ? o.threads
                            : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    for (const Place& place : o.places) {
        for (const int km : {200, 20, 2}) {
            const double metersPerPixel = km * 1000.0 / o.size;
            const std::vector<SurfaceBlend> blends =
                blendSquare(field, ecology, place.x, place.z, metersPerPixel, o.size, threads);
            const std::string stem = place.name + "_" + std::to_string(km) + "km";
            if (!writeMaps(o, stem, blends)) {
                std::fprintf(stderr, "Failed to write the maps %s into %s\n", stem.c_str(),
                             o.directory.string().c_str());
                return EXIT_FAILURE;
            }
        }
        std::printf("%s at (%.0f, %.0f): maps of 200, 20 and 2 km written\n", place.name.c_str(),
                    place.x, place.z);
    }
    return EXIT_SUCCESS;
}

// The blend of the column at (x, z) on the site of the LOD 0 tiles: slopes over one cell (1 m).
SurfaceBlend lod0Blend(const terrain::Heightfield& field, const terrain::Ecology& ecology, double x,
                       double z) {
    const terrain::HeightSample sample = field.sampleAt(x, z);
    const terrain::SurfaceSite site =
        terrain::heightfieldSite(sample.height, sample.continentalness,
                                 {field.heightAt(x + 1.0, z), field.heightAt(x - 1.0, z),
                                  field.heightAt(x, z + 1.0), field.heightAt(x, z - 1.0)},
                                 1.0);
    return ecology.blendAt(x, z, site, ecology.climate().sample(x, z, site));
}

// The point of the search lattice nearest to (x, z) holding sand with rock within one step of
// the lattice; found false without one.
bool findEdge(const terrain::Heightfield& field, const terrain::Ecology& ecology, double x,
              double z, double& edgeX, double& edgeZ) {
    const int half = static_cast<int>(kEdgeSearch / kSearchStep);
    const int side = 2 * half + 1;
    const double x0 = std::floor(x / kSearchStep) * kSearchStep - half * kSearchStep;
    const double z0 = std::floor(z / kSearchStep) * kSearchStep - half * kSearchStep;
    std::vector<terrain::SurfaceMaterial> materials(static_cast<std::size_t>(side * side));
    for (int k = 0; k < side; ++k) {
        for (int i = 0; i < side; ++i) {
            materials[static_cast<std::size_t>(k * side + i)] =
                lod0Blend(field, ecology, x0 + i * kSearchStep, z0 + k * kSearchStep).materials[0];
        }
    }
    const auto at = [&](int i, int k) { return materials[static_cast<std::size_t>(k * side + i)]; };
    double best = 1.0e300;
    for (int k = 1; k + 1 < side; ++k) {
        for (int i = 1; i + 1 < side; ++i) {
            const bool rockNear = at(i + 1, k) == terrain::SurfaceMaterial::Rock ||
                                  at(i - 1, k) == terrain::SurfaceMaterial::Rock ||
                                  at(i, k + 1) == terrain::SurfaceMaterial::Rock ||
                                  at(i, k - 1) == terrain::SurfaceMaterial::Rock;
            const double px = x0 + i * kSearchStep;
            const double pz = z0 + k * kSearchStep;
            const double d = terrain::dmath::hypot(px - x, pz - z);
            if (at(i, k) == terrain::SurfaceMaterial::Sand && rockNear && d < best) {
                best = d;
                edgeX = px;
                edgeZ = pz;
            }
        }
    }
    return best < 1.0e300;
}

// The texels of the LOD 0 tiles over the window from (x0, z0), every 4 m, row by row.
std::vector<terrain::BlendTexel> windowTexels(const terrain::TerrainTiles& tiles, double x0,
                                              double z0, int texels) {
    const double pitch = terrain::kTileBlendStride * terrain::tileCellMeters(0);
    std::vector<terrain::BlendTexel> result(static_cast<std::size_t>(texels * texels));
    const int chunks = static_cast<int>((texels - 1) * pitch / terrain::kChunkSizeMeters);
    const auto firstX = static_cast<int>(std::floor(x0 / terrain::kChunkSizeMeters));
    const auto firstZ = static_cast<int>(std::floor(z0 / terrain::kChunkSizeMeters));
    for (int cz = firstZ; cz < firstZ + chunks; ++cz) {
        for (int cx = firstX; cx < firstX + chunks; ++cx) {
            const terrain::TileData data = tiles.generate({{{cx, cz}, 0}, 0});
            for (int k = 0; k < terrain::kTileBlendTexels; ++k) {
                for (int i = 0; i < terrain::kTileBlendTexels; ++i) {
                    const int gi = (cx - firstX) * (terrain::kTileBlendTexels - 1) + i;
                    const int gk = (cz - firstZ) * (terrain::kTileBlendTexels - 1) + k;
                    result[static_cast<std::size_t>(gk * texels + gi)] =
                        data.surfaceBlend[static_cast<std::size_t>(k * terrain::kTileBlendTexels +
                                                                   i)];
                }
            }
        }
    }
    return result;
}

// Mean width of the band between sand and rock: the area where rock weighs 5 % to 95 % over the
// length of the line where it weighs one half (the pixel edges it crosses).
double bandWidth(const std::vector<BlendWeights>& weights, int size, double pixel) {
    const auto rock = [&](int i, int k) {
        return weights[static_cast<std::size_t>(k * size + i)]
            .materials[static_cast<std::size_t>(terrain::SurfaceMaterial::Rock)];
    };
    double band = 0.0;
    double line = 0.0;
    for (int k = 0; k < size; ++k) {
        for (int i = 0; i < size; ++i) {
            band += rock(i, k) > 0.05f && rock(i, k) < 0.95f ? 1.0 : 0.0;
            line += i + 1 < size && (rock(i, k) < 0.5f) != (rock(i + 1, k) < 0.5f) ? 1.0 : 0.0;
            line += k + 1 < size && (rock(i, k) < 0.5f) != (rock(i, k + 1) < 0.5f) ? 1.0 : 0.0;
        }
    }
    return line > 0.0 ? band * pixel / line : 0.0;
}

int runEdge(const Options& o) {
    const terrain::TerrainTiles tiles(o.seed);
    const terrain::Heightfield& field = tiles.heightfield();
    const terrain::Ecology& ecology = tiles.ecology();
    double cx = 0.0;
    double cz = 0.0;
    if (!findEdge(field, ecology, o.x, o.z, cx, cz)) {
        std::fprintf(stderr, "No edge between sand and rock within %.0f m of (%.0f, %.0f)\n",
                     kEdgeSearch, o.x, o.z);
        return EXIT_FAILURE;
    }
    const double x0 = std::floor((cx - kEdgeWindow / 2.0) / terrain::kChunkSizeMeters) *
                      terrain::kChunkSizeMeters;
    const double z0 = std::floor((cz - kEdgeWindow / 2.0) / terrain::kChunkSizeMeters) *
                      terrain::kChunkSizeMeters;
    const double pitch = terrain::kTileBlendStride * terrain::tileCellMeters(0);
    const int texels = static_cast<int>(kEdgeWindow / pitch) + 1;
    const std::vector<terrain::BlendTexel> grid = windowTexels(tiles, x0, z0, texels);
    const int size = static_cast<int>(kEdgeWindow / kEdgePixel);
    std::vector<BlendWeights> before;
    std::vector<BlendWeights> reference;
    std::vector<BlendWeights> blended(static_cast<std::size_t>(size * size));
    for (int k = 0; k < size; ++k) {
        for (int i = 0; i < size; ++i) {
            const double x = x0 + (i + 0.5) * kEdgePixel;
            const double z = z0 + (k + 0.5) * kEdgePixel;
            const SurfaceBlend blend = lod0Blend(field, ecology, x, z);
            before.push_back(terrain::dominantWeights(blend));
            reference.push_back(terrain::blendWeights(blend));
            const double fx = (x - x0) / pitch;
            const double fz = (z - z0) / pitch;
            const int ti = static_cast<int>(fx);
            const int tk = static_cast<int>(fz);
            const auto u = static_cast<float>(fx - ti);
            const auto v = static_cast<float>(fz - tk);
            BlendWeights& w = blended[static_cast<std::size_t>(k * size + i)];
            const auto texel = [&](int a, int b) {
                return grid[static_cast<std::size_t>(b * texels + a)];
            };
            terrain::addTexel(w, texel(ti, tk), (1.0f - u) * (1.0f - v));
            terrain::addTexel(w, texel(ti + 1, tk), u * (1.0f - v));
            terrain::addTexel(w, texel(ti, tk + 1), (1.0f - u) * v);
            terrain::addTexel(w, texel(ti + 1, tk + 1), u * v);
        }
    }
    const bool written =
        terrain::writePng(o.directory / "edge_before.png",
                          terrain::renderMixedMaterialMap(before, size, size)) &&
        terrain::writePng(o.directory / "edge_reference.png",
                          terrain::renderMixedMaterialMap(reference, size, size)) &&
        terrain::writePng(o.directory / "edge_texels.png",
                          terrain::renderMixedMaterialMap(blended, size, size));
    if (!written) {
        std::fprintf(stderr, "Failed to write the edge maps into %s\n",
                     o.directory.string().c_str());
        return EXIT_FAILURE;
    }
    std::printf(
        "edge between sand and rock at (%.0f, %.0f), window from (%.0f, %.0f), %.0f m: band "
        "between sand and rock %.1f m wide every 0.5 m, %.1f m from the texels of 4 m\n",
        cx, cz, x0, z0, kEdgeWindow, bandWidth(reference, size, kEdgePixel),
        bandWidth(blended, size, kEdgePixel));
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::fprintf(stderr,
                     "Invalid arguments\n"
                     "Usage: %s maps <output-dir> [--seed <s>] [--size <px>] [--threads <n>] "
                     "--at <name> <x> <z> [--at <name> <x> <z>]...\n"
                     "       %s edge <output-dir> <x> <z> [--seed <s>]\n",
                     argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    std::error_code error;
    std::filesystem::create_directories(options.directory, error);
    return options.mode == "maps" ? runMaps(options) : runEdge(options);
}
