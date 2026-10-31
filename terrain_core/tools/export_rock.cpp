// Views of the rock of the streamed world (phase 12A): the tiles a client streams around an eye
// (LOD 0 to 3 with their seams, the water of the rivers and lakes over them), drawn as meshes over
// the view of the relief beyond them, and views from straight above of the LOD 0 tiles of a
// square. The places (rock_places.hpp): the highest wall by the K2 near, far and across the edge
// of the tiles; the fall of 388 m from its foot, its slot and from above; a gorge near it. Writes
// rock_<name>.png and top_<name>.png into the output directory, and prints (and writes to
// rock.txt) where each view stands, the tiles it draws and the time they take.
// Usage: terrain_export_rock <output-dir> [--planet <file>] [--threads <n>] [--width <pixels>]
//        [--view-distance <chunks>] [--only <name>]... [--no-rock]
//        [--at <x> <z> <heading degrees> <tilt> <meters above the ground>] [--sun <turns>]
//        [--cost] [--check] [--face <x> <z>] [--walk <x> <z>] [--steps]
// --cost: the cost of the rock in the tiles around a wall, the rivers, the plains and crests.
// --check: also check_<name>.png, the same view with the openings of the tiles in yellow, their
// inverted triangles in red and the triangles seen from behind in cyan (mesh_checks.hpp), and
// their counts.
// --face: face_side, the tallest face near (x, z) under a sun across it (rock_spots.hpp).
// --walk: where to walk from straight to a face and under an overhang near (x, z), and the views
// from there (walk_face, walk_under).
// --steps: the eyes on the steps of the fall of 388 m (rock_steps.hpp), without a view but
// those --only names.
// --no-rock: the tiles of the relief alone (a height per column), the views before phase 12A.
// The planet level is read from --planet (or <output-dir>/planet.bin), built and written there
// when it does not hold it (about 100 s on 4 cores).

#include "rock_cost.hpp"
#include "rock_places.hpp"
#include "rock_report.hpp"
#include "rock_spots.hpp"
#include "rock_steps.hpp"

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/io/mesh_checks.hpp"
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
#include <optional>
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

using rock_tool::format;
using rock_tool::Places;
using rock_tool::Report;
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
constexpr double kBigStep = 10.0; // --steps: meters of the drop of a step at least

struct Options {
    std::filesystem::path directory;
    std::filesystem::path planet;
    int threads = 0;
    int width = 1600;
    int viewDistance = 53; // chunks: the tiles end about 850 m from the eye, as in Unreal
    bool rock = true;      // --no-rock: the tiles of the relief alone, a height per column
    bool cost = false;     // --cost: the cost of the rock at the places instead of the views
    bool check = false;    // --check: the views of the checks of the meshes too
    bool steps = false;    // --steps: the eyes on the steps of the fall of 388 m
    std::vector<std::string> only;
    // --at <x> <z> <heading degrees> <tilt> <meters above the ground>: a view "at" there, and
    // "at_top" from above, 100 m wide.
    std::optional<std::array<double, 5>> at;
    double sunLeft = 0.3; // --sun <turns>: of the sun of the view --at, left of its axis
    std::optional<std::array<double, 2>> face; // --face <x> <z>
    std::optional<std::array<double, 2>> walk; // --walk <x> <z>
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
        if (flag == "--cost" || flag == "--check" || flag == "--steps") {
            (flag == "--cost" ? o.cost : flag == "--check" ? o.check : o.steps) = true;
            continue;
        }
        if (flag == "--at") {
            if (i + 5 >= argc) {
                return false;
            }
            std::array<double, 5> at{};
            for (double& v : at) {
                v = std::atof(argv[++i]);
            }
            o.at = at;
            continue;
        }
        if (flag == "--face" || flag == "--walk") {
            if (i + 2 >= argc) {
                return false;
            }
            const std::array<double, 2> at{std::atof(argv[i + 1]), std::atof(argv[i + 2])};
            (flag == "--face" ? o.face : o.walk) = at;
            i += 2;
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
        } else if (flag == "--sun") {
            o.sunLeft = std::atof(value);
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
    std::vector<terrain::MeshCheck> checks; // with --check, of each tile
    std::vector<terrain::ViewMesh> meshes;
    std::size_t triangles = 0;
};

Drawn drawn(const World& w, const std::vector<TileData>& tiles, bool check) {
    Drawn d;
    d.water.reserve(tiles.size());
    for (const TileData& t : tiles) {
        d.water.push_back(waterOf(w.field, t));
        if (check) {
            d.checks.push_back(terrain::checkTileMesh(
                t.mesh, static_cast<float>(terrain::tileSizeMeters(t.request.key.lod))));
        }
    }
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const TileData& t = tiles[i];
        d.meshes.push_back({&t.mesh, t.originX, t.originY, t.originZ, {}, false, {}});
        d.meshes.push_back({&d.water[i], t.originX, t.originY, t.originZ, {}, true, {}});
        d.triangles += t.mesh.indices.size() / 3;
    }
    return d;
}

// The view of the checks of the meshes of a shot, and their counts by LOD.
bool writeCheck(Drawn& d, const std::vector<TileData>& tiles, const terrain::Image& background,
                const terrain::ViewParams& p, const Options& o, const Shot& s, Report& r) {
    std::array<terrain::MeshCheck, terrain::kLodCount> sums{};
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const terrain::MeshCheck& c = d.checks[i];
        d.meshes[2 * i].marks = c.marks;
        terrain::MeshCheck& sum = sums[static_cast<std::size_t>(tiles[i].request.key.lod)];
        sum.triangles += c.triangles;
        sum.degenerate += c.degenerate;
        sum.openEdges += c.openEdges;
        sum.nonManifoldEdges += c.nonManifoldEdges;
        sum.inverted += c.inverted;
    }
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::MeshCheck& c = sums[static_cast<std::size_t>(lod)];
        r.add(format("  check LOD %d: %zu triangles, %zu degenerate, %zu open edges, %zu not "
                     "manifold, %zu inverted\n",
                     lod, c.triangles, c.degenerate, c.openEdges, c.nonManifoldEdges, c.inverted));
    }
    const terrain::Image image = terrain::renderMeshView(d.meshes, background, p, o.threads,
                                                         {s.sunLeft, s.sunSine, true});
    return terrain::writePng(o.directory / ("check_" + s.name + ".png"), image);
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
    // On the ground, the top of its rock (an overhang may stand over the relief there).
    p.eyeHeight = static_cast<double>(wet ? at.waterLevel : w.tiles.groundAt(s.x, s.z)) + s.above;
    p.heading = s.heading;
    p.tilt = s.tilt;
    p.width = o.width;
    p.height = s.height * o.width / 1600;
    terrain::LodParams lod;
    lod.viewDistance = o.viewDistance;
    const std::vector<TileRequest> requests =
        terrain::tileRequests(terrain::selectTiles(terrain::worldToChunk(s.x, s.z), lod));
    const Generated g = generate(w, requests, o.threads);
    Drawn d = drawn(w, g.tiles, o.check);
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
        terrain::renderMeshView(d.meshes, background, p, o.threads, {s.sunLeft, s.sunSine});
    const std::string file = "rock_" + s.name + ".png";
    r.add(format("%s: eye at (%.0f, %.0f), %.1f m (%s %.1f m), heading %.1f degrees, tilt %.2f; "
                 "%s; %zu triangles\n  %s\n",
                 file.c_str(), s.x, s.z, p.eyeHeight, wet ? "water" : "ground", s.above,
                 360.0 * s.heading, s.tilt, timings(g).c_str(), d.triangles, s.note.c_str()));
    return terrain::writePng(o.directory / file, image) &&
           (!o.check || writeCheck(d, g.tiles, background, p, o, s, r));
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
    const Drawn d = drawn(w, g.tiles, false);
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

// The cost of the rock around the places: a wall (the K2), the foot of the falls (the rivers),
// the plains of the center of the tests and the crests by the K2; then no view.
void reportCost(const World& w, Options& o, const Places& places, Report& r) {
    const terrain::TerrainTiles relief(terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {},
                                       w.hydrology, false);
    std::vector<rock_tool::CostPlace> at = {{"plains", -1'245'184.0, -8'500'224.0},
                                            {"crests", 416'857.0, -11'405'966.0}};
    for (const Shot& s : places.shots) {
        if (s.name == "k2_wall_near" || s.name == "fall_foot") {
            at.push_back({s.name == "k2_wall_near" ? "wall" : "rivers", s.x, s.z});
        }
    }
    for (const rock_tool::CostPlace& p : at) {
        r.add(rock_tool::costAt(w.tiles, relief, p, o.viewDistance, 3));
    }
    for (const TopShot& s : places.tops) {
        if (s.name == "k2_face_top" || s.name == "k2_wall_top") {
            r.add(rock_tool::lodJumps(w.tiles, relief, {s.name, s.x, s.z}, 128.0,
                                      o.directory.string(), o.threads));
        }
    }
    o.only = {"none"};
}

// The places the options ask for: the eyes on the steps (--steps), the cost (--cost), a view
// (--at), a face (--face), a walk (--walk).
void addAsked(const World& w, Options& o, Places& places, Report& r) {
    if (o.steps) {
        r.add(rock_tool::addSteps(w.field, *w.hydrology, kFallX, kFallZ, kBigStep, places));
        o.only = o.only.empty() ? std::vector<std::string>{"none"} : o.only;
    }
    if (o.cost) {
        reportCost(w, o, places, r);
    }
    if (o.at) {
        const std::array<double, 5>& a = *o.at;
        places.shots.push_back(
            {"at", a[0], a[1], a[4], a[2] / 360.0, a[3], 900, o.sunLeft, "--at", 0.5});
        const double side = 4.0 * a[4] + 4.0;
        places.tops.push_back({"at_top", a[0], a[1], side, side / 1000.0, "--at"});
    }
    if (o.face && !rock_tool::addFaceSide(w.field, (*o.face)[0], (*o.face)[1], places)) {
        r.add("no face near --face\n");
    }
    if (o.walk) {
        for (const bool under : {false, true}) {
            const rock_tool::WalkSpot spot =
                rock_tool::walkToFace(w.tiles, (*o.walk)[0], (*o.walk)[1], under);
            r.add(rock_tool::describe(spot, under) + "\n");
            if (spot.found) {
                places.shots.push_back(rock_tool::walkShot(spot, under ? "walk_under" : "walk_face"));
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <output-dir> [--planet <file>] [--threads <n>] [--width <pixels>] "
                     "[--view-distance <chunks>] [--only <name>]... [--no-rock] "
                     "[--at <x> <z> <heading> <tilt> <above>] [--sun <turns>] [--cost] [--check] "
                     "[--face <x> <z>] [--walk <x> <z>] [--steps]\n",
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
    rock_tool::addFallViews(places);
    addAsked(w, o, places, r);
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
