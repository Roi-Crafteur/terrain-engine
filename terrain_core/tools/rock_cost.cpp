// The cost of the rock in three dimensions in the tiles (rock_cost.hpp).
#include "rock_cost.hpp"

#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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

} // namespace

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
            c.rockSeconds = std::min(c.rockSeconds,
                                     secondsOf(rock, requests, l, first ? &c.rockTriangles : nullptr,
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
