// Views of the rock of the streamed world (phase 12A): the tiles a client streams around an eye
// (LOD 0 to 3 with their seams, the water of the rivers and lakes over them), drawn as meshes over
// the view of the relief beyond them, and views from straight above of the LOD 0 tiles of a
// square. The places (rock_places.hpp): the highest wall by the K2 near, far and across the edge
// of the tiles; the fall of 388 m from its foot, its slot and from above; a gorge near it. Writes
// rock_<name>.png and top_<name>.png into the output directory, and prints (and writes to
// rock.txt) where each view stands, the tiles it draws and the time they take.
// Usage: terrain_export_rock <output-dir> [--planet <file>] [--threads <n>] [--width <pixels>]
//        [--view-distance <chunks>] [--only <name>]... [--no-rock]
// --no-rock: the tiles of the relief alone (a height per column), the views before phase 12A.
// The planet level is read from --planet (or <output-dir>/planet.bin), built and written there
// when it does not hold it (about 100 s on 4 cores).

#include "rock_places.hpp"

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/io/mesh_view.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/relief_view.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

using rock_tool::Places;
using rock_tool::Shot;
using rock_tool::TopShot;
using terrain::TileData;
using terrain::TileRequest;

// The fall of 388 m seen in Unreal after phase 11B.
constexpr double kFallX = -6'185'408.0;
constexpr double kFallZ = 156'215.0;
// The level of water drawn over a cell: its 4 corners under water within kFlatWater meters of
// each other (the pools, not the water falling down a face).
constexpr float kFlatWater = 1.0f;

struct Options {
    std::filesystem::path directory;
    std::filesystem::path planet;
    int threads = 0;
    int width = 1600;
    int viewDistance = 53; // chunks: the tiles end about 850 m from the eye, as in Unreal
    bool rock = true;      // --no-rock: the tiles of the relief alone, a height per column
    std::vector<std::string> only;
};

bool parseOptions(int argc, char** argv, Options& o) {
    if (argc < 2 || argv[1][0] == '-') {
        return false;
    }
    o.directory = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--no-rock") {
            o.rock = false;
            continue;
        }
        if (i + 1 >= argc) {
            return false;
        }
        const char* value = argv[++i];
        if (flag == "--planet") {
            o.planet = value;
        } else if (flag == "--threads") {
            o.threads = std::atoi(value);
        } else if (flag == "--width") {
            o.width = std::atoi(value);
        } else if (flag == "--view-distance") {
            o.viewDistance = std::atoi(value);
        } else if (flag == "--only") {
            o.only.emplace_back(value);
        } else {
            return false;
        }
    }
    return o.width >= 16 && o.viewDistance >= 16 && o.threads >= 0;
}

bool wanted(const Options& o, const std::string& name) {
    return o.only.empty() || std::find(o.only.begin(), o.only.end(), name) != o.only.end();
}

class Report {
public:
    void add(const std::string& line) {
        std::fputs(line.c_str(), stdout);
        text_ += line;
    }
    [[nodiscard]] bool write(const std::filesystem::path& path) const {
        std::FILE* file = std::fopen(path.string().c_str(), "w");
        if (file == nullptr) {
            return false;
        }
        const bool ok = std::fputs(text_.c_str(), file) >= 0;
        return std::fclose(file) == 0 && ok;
    }

private:
    std::string text_;
};

template <typename... Args>
std::string format(const char* pattern, Args... args) {
    char line[512];
    std::snprintf(line, sizeof line, pattern, args...);
    return line;
}

struct World {
    terrain::Heightfield relief;
    std::shared_ptr<const terrain::Hydrology> hydrology;
    terrain::Heightfield field;
    terrain::TerrainTiles tiles;
};

// The tiles of the requests, generated on the threads (each into its own slot), and the seconds
// each LOD took in all.
struct Generated {
    std::vector<TileData> tiles;
    std::array<double, terrain::kLodCount> seconds{};
    std::array<int, terrain::kLodCount> counts{};
};

Generated generate(const World& w, const std::vector<TileRequest>& requests, int threads) {
    Generated out;
    out.tiles.resize(requests.size());
    std::vector<double> seconds(requests.size(), 0.0);
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (std::size_t i = next++; i < requests.size(); i = next++) {
            const auto start = std::chrono::steady_clock::now();
            out.tiles[i] = w.tiles.generate(requests[i]);
            seconds[i] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                             .count();
        }
    };
    std::vector<std::jthread> workers;
    for (int t = 1; t < std::max(1, threads); ++t) {
        workers.emplace_back(work);
    }
    work();
    workers.clear();
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto lod = static_cast<std::size_t>(requests[i].key.lod);
        out.seconds[lod] += seconds[i];
        ++out.counts[lod];
    }
    return out;
}

// The flat water over the columns of a tile (its pools, lakes and rivers), relative to its origin.
terrain::Mesh waterOf(const terrain::Heightfield& field, const TileData& tile) {
    const auto spacing = terrain::tileCellMeters(tile.request.key.lod);
    constexpr int kSide = terrain::kGridSamples;
    std::vector<double> xs;
    std::vector<double> zs;
    for (int k = 0; k < kSide; ++k) {
        for (int i = 0; i < kSide; ++i) {
            xs.push_back(tile.originX + i * spacing);
            zs.push_back(tile.originZ + k * spacing);
        }
    }
    std::vector<terrain::HeightSample> s(xs.size());
    field.samplesAt(xs, zs, s);
    const auto level = [&](int i, int k) {
        const terrain::HeightSample& at = s[static_cast<std::size_t>(k * kSide + i)];
        return at.water == terrain::WaterKind::None ? -std::numeric_limits<float>::infinity()
                                                    : at.waterLevel;
    };
    terrain::Mesh m;
    for (int k = 0; k + 1 < kSide; ++k) {
        for (int i = 0; i + 1 < kSide; ++i) {
            const float l[4] = {level(i, k), level(i + 1, k), level(i + 1, k + 1),
                                level(i, k + 1)};
            const auto [low, high] = std::minmax({l[0], l[1], l[2], l[3]});
            if (!std::isfinite(low) || high - low > kFlatWater) {
                continue;
            }
            const auto first = static_cast<std::uint32_t>(m.vertices.size());
            const int corner[4][2] = {{i, k}, {i + 1, k}, {i + 1, k + 1}, {i, k + 1}};
            for (int c = 0; c < 4; ++c) {
                m.vertices.push_back({static_cast<float>(corner[c][0] * spacing),
                                      static_cast<float>(l[c] - tile.originY),
                                      static_cast<float>(corner[c][1] * spacing)});
                m.normals.push_back({0.0f, 1.0f, 0.0f});
            }
            for (const std::uint32_t j : {0U, 3U, 2U, 0U, 2U, 1U}) {
                m.indices.push_back(first + j);
            }
        }
    }
    return m;
}

struct Drawn {
    std::vector<terrain::Mesh> water;
    std::vector<terrain::ViewMesh> meshes;
    std::size_t triangles = 0;
};

Drawn drawn(const World& w, const std::vector<TileData>& tiles) {
    Drawn d;
    d.water.reserve(tiles.size());
    for (const TileData& t : tiles) {
        d.water.push_back(waterOf(w.field, t));
    }
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const TileData& t = tiles[i];
        d.meshes.push_back({&t.mesh, t.originX, t.originY, t.originZ, {}, false});
        d.meshes.push_back({&d.water[i], t.originX, t.originY, t.originZ, {}, true});
        d.triangles += t.mesh.indices.size() / 3;
    }
    return d;
}

std::string timings(const Generated& g) {
    std::string out = "tiles";
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const auto l = static_cast<std::size_t>(lod);
        out += format(" LOD %d: %d (%.2f ms each)", lod, g.counts[l],
                      g.counts[l] > 0 ? 1000.0 * g.seconds[l] / g.counts[l] : 0.0);
    }
    return out;
}

bool writeShot(const World& w, const Options& o, const Shot& s, Report& r) {
    const terrain::HeightSample at = w.field.sampleAt(s.x, s.z);
    const bool wet = at.water != terrain::WaterKind::None && at.waterLevel > at.height;
    terrain::ViewParams p;
    p.eyeX = s.x;
    p.eyeZ = s.z;
    p.eyeHeight = static_cast<double>(wet ? at.waterLevel : at.height) + s.above;
    p.heading = s.heading;
    p.tilt = s.tilt;
    p.width = o.width;
    p.height = s.height * o.width / 1600;
    terrain::LodParams lod;
    lod.viewDistance = o.viewDistance;
    const std::vector<TileRequest> requests =
        terrain::tileRequests(terrain::selectTiles(terrain::worldToChunk(s.x, s.z), lod));
    const Generated g = generate(w, requests, o.threads);
    const Drawn d = drawn(w, g.tiles);
    // The relief beyond the tiles: from well inside them, which they cover where they are.
    terrain::ViewParams far = p;
    far.minDistance = 0.7 * o.viewDistance * terrain::kChunkSizeMeters;
    const terrain::ViewGround ground = [&w](double x, double z) { return w.field.heightAt(x, z); };
    const terrain::ViewWater water = [&w](double x, double z) {
        const terrain::HeightSample h = w.field.sampleAt(x, z);
        return h.water == terrain::WaterKind::None ? -std::numeric_limits<float>::infinity()
                                                   : h.waterLevel;
    };
    const terrain::Image background = terrain::renderReliefView(ground, water, far, o.threads);
    const terrain::Image image =
        terrain::renderMeshView(d.meshes, background, p, o.threads, {s.sunLeft, 0.5});
    const std::string file = "rock_" + s.name + ".png";
    r.add(format("%s: eye at (%.0f, %.0f), %.1f m (%s %.1f m), heading %.1f degrees, tilt %.2f; "
                 "%s; %zu triangles\n  %s\n",
                 file.c_str(), s.x, s.z, p.eyeHeight, wet ? "water" : "ground", s.above,
                 360.0 * s.heading, s.tilt, timings(g).c_str(), d.triangles, s.note.c_str()));
    return terrain::writePng(o.directory / file, image);
}

bool writeTop(const World& w, const Options& o, const TopShot& s, Report& r) {
    const int chunks = static_cast<int>(std::ceil(0.5 * s.side / terrain::kChunkSizeMeters)) + 1;
    const terrain::ChunkCoord center = terrain::worldToChunk(s.x, s.z);
    std::vector<TileRequest> requests;
    for (int dz = -chunks; dz <= chunks; ++dz) {
        for (int dx = -chunks; dx <= chunks; ++dx) {
            requests.push_back({{{center.x + dx, center.z + dz}, 0}, 0});
        }
    }
    const Generated g = generate(w, requests, o.threads);
    const Drawn d = drawn(w, g.tiles);
    terrain::TopViewParams p;
    p.centerX = s.x;
    p.centerZ = s.z;
    p.metersPerPixel = s.metersPerPixel;
    p.width = static_cast<int>(std::lround(s.side / s.metersPerPixel));
    p.height = p.width;
    const std::string file = "top_" + s.name + ".png";
    r.add(format("%s: %.0f m around (%.0f, %.0f), %.2f m per pixel; %s; %zu triangles\n  %s\n",
                 file.c_str(), s.side, s.x, s.z, s.metersPerPixel, timings(g).c_str(),
                 d.triangles, s.note.c_str()));
    return terrain::writePng(o.directory / file, terrain::renderTopView(d.meshes, p, o.threads));
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [--planet <file>] [--threads <n>] [--width <pixels>] "
                     "[--view-distance <chunks>] [--only <name>]... [--no-rock]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    std::error_code error;
    std::filesystem::create_directories(o.directory, error);
    const auto start = std::chrono::steady_clock::now();
    terrain::PlanetParams params;
    params.threads = o.threads;
    const std::filesystem::path planetFile =
        o.planet.empty() ? o.directory / terrain::kPlanetFileName : o.planet;
    const std::uint64_t seed = terrain::kDefaultWorldSeed;
    bool built = false;
    auto planet = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
        planetFile, seed, terrain::HeightfieldParams{}, params, terrain::WorldSize{}, &built));
    const terrain::Heightfield relief(seed);
    auto hydrology = std::make_shared<const terrain::Hydrology>(seed, relief, planet);
    const World w{relief, hydrology, relief.withHydrology(hydrology),
                  terrain::TerrainTiles(seed, {}, {}, std::nullopt, {}, hydrology, o.rock)};
    Report r;
    r.add(format("rock of seed %llu: planet level %s %s in %.1f s; tiles to %d chunks%s\n",
                 static_cast<unsigned long long>(seed), built ? "built and written to" : "read from",
                 planetFile.string().c_str(),
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                 o.viewDistance, o.rock ? "" : ", without the rock (--no-rock)"));
    Places places;
    rock_tool::addK2(relief, o.viewDistance * terrain::kChunkSizeMeters, places);
    if (!rock_tool::addFall(*hydrology, kFallX, kFallZ, places)) {
        r.add("no fall near the fall of 388 m\n");
    }
    if (!rock_tool::addGorge(*hydrology, kFallX, kFallZ, places)) {
        r.add("no gorge near the fall of 388 m\n");
    }
    bool written = true;
    for (const Shot& s : places.shots) {
        written = (!wanted(o, s.name) || writeShot(w, o, s, r)) && written;
    }
    for (const TopShot& s : places.tops) {
        written = (!wanted(o, s.name) || writeTop(w, o, s, r)) && written;
    }
    r.add(format("%.1f s in all\n",
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()));
    if (!(r.write(o.directory / "rock.txt") && written)) {
        std::fprintf(stderr, "Could not write every file to %s\n", o.directory.string().c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
