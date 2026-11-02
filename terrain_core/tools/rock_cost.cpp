// The cost of the rock in three dimensions in the tiles (rock_cost.hpp).
#include "rock_cost.hpp"

#include "terrain/io/mesh_view.hpp"
#include "terrain/io/png.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <vector>

namespace rock_tool {

namespace {

using terrain::TileRequest;

struct LodCost {
    double rockSeconds = 1e30; // the best round
    double reliefSeconds = 1e30;
    int tiles = 0;
    int withFaces = 0;
    std::size_t rockTriangles = 0;
    std::size_t reliefTriangles = 0;
};

double secondsOf(const terrain::TerrainTiles& tiles, const std::vector<TileRequest>& requests,
                 int lod, std::size_t* triangles, int* withFaces) {
    const auto start = std::chrono::steady_clock::now();
    for (const TileRequest& r : requests) {
        if (r.key.lod != lod) {
            continue;
        }
        const terrain::TileData data = tiles.generate(r);
        if (triangles != nullptr) {
            *triangles += data.mesh.indices.size() / 3;
        }
        if (withFaces != nullptr) {
            *withFaces += data.rock.empty() ? 0 : 1;
        }
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The view from above of the tiles of LOD lod covering a square of side meters around (x, z).
terrain::Image topAtLod(const terrain::TerrainTiles& tiles, double x, double z, double side,
                        int lod, int threads) {
    const terrain::ChunkCoord low = terrain::worldToChunk(x - 0.5 * side, z - 0.5 * side);
    const terrain::ChunkCoord high = terrain::worldToChunk(x + 0.5 * side, z + 0.5 * side);
    std::vector<terrain::TileKey> keys;
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            const terrain::TileKey key = terrain::tileContaining({cx, cz}, lod);
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        }
    }
    std::vector<terrain::TileData> data;
    data.reserve(keys.size());
    for (const terrain::TileKey& key : keys) {
        data.push_back(tiles.generate({key, 0}));
    }
    std::vector<terrain::ViewMesh> meshes;
    for (const terrain::TileData& t : data) {
        meshes.push_back({&t.mesh, t.originX, t.originY, t.originZ, {}, false, {}});
    }
    terrain::TopViewParams p;
    p.centerX = x;
    p.centerZ = z;
    p.width = static_cast<int>(std::lround(side / p.metersPerPixel));
    p.height = p.width;
    return terrain::renderTopView(meshes, p, threads);
}

double meanDifference(const terrain::Image& a, const terrain::Image& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        sum += std::abs(static_cast<int>(a.pixels[i]) - static_cast<int>(b.pixels[i]));
    }
    return a.pixels.empty() ? 0.0 : sum / static_cast<double>(a.pixels.size());
}

} // namespace

std::string lodJumps(const terrain::TerrainTiles& rock, const terrain::TerrainTiles& relief,
                     const CostPlace& place, double side, const std::string& directory,
                     int threads) {
    std::array<terrain::Image, terrain::kLodCount> withRock;
    std::array<terrain::Image, terrain::kLodCount> alone;
    for (int l = 0; l < terrain::kLodCount; ++l) {
        withRock[static_cast<std::size_t>(l)] = topAtLod(rock, place.x, place.z, side, l, threads);
        alone[static_cast<std::size_t>(l)] = topAtLod(relief, place.x, place.z, side, l, threads);
        static_cast<void>(terrain::writePng(directory + "/lod" + std::to_string(l) + "_" +
                                                place.name + ".png",
                                            withRock[static_cast<std::size_t>(l)]));
    }
    std::string out;
    for (int l = 0; l + 1 < terrain::kLodCount; ++l) {
        const auto a = static_cast<std::size_t>(l);
        char line[256];
        std::snprintf(line, sizeof line,
                      "%s, LOD %d to %d: the view from above changes by %.1f levels with the "
                      "rock, %.1f without\n",
                      place.name.c_str(), l, l + 1, meanDifference(withRock[a], withRock[a + 1]),
                      meanDifference(alone[a], alone[a + 1]));
        out += line;
    }
    return out;
}

std::string costAt(const terrain::TerrainTiles& rock, const terrain::TerrainTiles& relief,
                   const CostPlace& place, int viewDistance, int rounds) {
    terrain::LodParams lod;
    lod.viewDistance = viewDistance;
    const std::vector<TileRequest> requests =
        terrain::tileRequests(terrain::selectTiles(terrain::worldToChunk(place.x, place.z), lod));
    std::string out;
    for (int l = 0; l < terrain::kLodCount; ++l) {
        LodCost c;
        for (const TileRequest& r : requests) {
            c.tiles += r.key.lod == l ? 1 : 0;
        }
        for (int round = 0; round < rounds; ++round) {
            const bool first = round == 0;
            c.rockSeconds =
                std::min(c.rockSeconds, secondsOf(rock, requests, l,
                                                  first ? &c.rockTriangles : nullptr,
                                                  first ? &c.withFaces : nullptr));
            c.reliefSeconds = std::min(
                c.reliefSeconds,
                secondsOf(relief, requests, l, first ? &c.reliefTriangles : nullptr, nullptr));
        }
        const double perTile = c.tiles > 0 ? 1000.0 / c.tiles : 0.0;
        char line[320];
        std::snprintf(line, sizeof line,
                      "%s LOD %d: %d tiles, %d with faces (%.0f%%); %.3f ms per tile with the "
                      "rock, %.3f without (x%.2f); %zu triangles, %zu without\n",
                      place.name.c_str(), l, c.tiles, c.withFaces,
                      c.tiles > 0 ? 100.0 * c.withFaces / c.tiles : 0.0, c.rockSeconds * perTile,
                      c.reliefSeconds * perTile,
                      c.reliefSeconds > 0.0 ? c.rockSeconds / c.reliefSeconds : 0.0,
                      c.rockTriangles, c.reliefTriangles);
        out += line;
    }
    return out;
}

} // namespace rock_tool
