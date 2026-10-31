#include "load_schedule.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <tuple>

namespace load_schedule {

namespace {

using RequestId = std::tuple<int, int, int, unsigned>;

RequestId idOf(const terrain::TileRequest& request) {
    return {request.key.origin.x, request.key.origin.z, request.key.lod, request.seams};
}

using Costs = std::map<RequestId, double>;

// The tiles of a selection around the chunk, nearest first, each generated once (on this thread)
// to measure its cost; a tile measured before keeps its cost.
std::vector<Tile> measured(const terrain::TerrainTiles& tiles, terrain::ChunkCoord chunk,
                           const terrain::LodParams& lod, Costs& costs) {
    std::vector<Tile> result;
    for (const terrain::TileRequest& request :
         terrain::tileRequests(terrain::selectTiles(chunk, lod))) {
        const auto [it, inserted] = costs.try_emplace(idOf(request), 0.0);
        if (inserted) {
            const auto start = std::chrono::steady_clock::now();
            [[maybe_unused]] const terrain::TileData data = tiles.generate(request);
            it->second = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                             .count();
        }
        result.push_back({request, terrain::tileDistance(request.key, chunk), it->second});
    }
    return result;
}

std::vector<double> costsOf(std::span<const Tile> tiles) {
    std::vector<double> costs;
    costs.reserve(tiles.size());
    for (const Tile& tile : tiles) {
        costs.push_back(tile.cost);
    }
    return costs;
}

std::string seconds2(double seconds) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f s", seconds);
    return text;
}

std::string describeSelection(const char* name, std::span<const Tile> tiles) {
    std::array<int, terrain::kLodCount> count{};
    std::array<double, terrain::kLodCount> cost{};
    double total = 0.0;
    for (const Tile& tile : tiles) {
        const auto lod = static_cast<std::size_t>(tile.request.key.lod);
        ++count[lod];
        cost[lod] += tile.cost;
        total += tile.cost;
    }
    std::string line = std::string("  ") + name + ": " + std::to_string(tiles.size()) + " tiles";
    for (std::size_t lod = 0; lod < count.size(); ++lod) {
        char text[64];
        std::snprintf(text, sizeof(text), ", LOD %zu %d x %.2f ms", lod, count[lod],
                      count[lod] == 0 ? 0.0 : cost[lod] * 1000.0 / count[lod]);
        line += text;
    }
    return line + ", " + seconds2(total) + " on one thread";
}

// The coarse stage then the tiles of the selection it does not hold; the index in that order of
// every tile of the selection.
std::vector<Tile> coarseOrder(std::span<const Tile> coarse, std::span<const Tile> selection,
                              std::vector<std::size_t>& selectionIndex) {
    std::vector<Tile> order(coarse.begin(), coarse.end());
    std::map<RequestId, std::size_t> index;
    for (std::size_t i = 0; i < coarse.size(); ++i) {
        index.emplace(idOf(coarse[i].request), i);
    }
    selectionIndex.clear();
    for (const Tile& tile : selection) {
        const auto it = index.find(idOf(tile.request));
        if (it != index.end()) {
            selectionIndex.push_back(it->second);
        } else {
            selectionIndex.push_back(order.size());
            order.push_back(tile);
        }
    }
    return order;
}

std::string simulateCase(std::span<const Tile> selection, std::span<const Tile> coarse,
                         const Params& params) {
    const std::vector<double> today = simulate(costsOf(selection), params);
    std::vector<std::size_t> index;
    const std::vector<Tile> order = coarseOrder(coarse, selection, index);
    const std::vector<double> staged = simulate(costsOf(order), params);
    std::vector<double> selectionShown;
    for (const std::size_t i : index) {
        selectionShown.push_back(staged[i]);
    }
    const std::span<const double> coarseShown(staged.data(), coarse.size());
    char text[400];
    std::snprintf(
        text, sizeof(text),
        "  %2d workers, %2zu per frame: today ground %s, 64 m %s, 256 m %s, all %s | coarse "
        "stage ground %s, covered 64 m %s, 256 m %s, all %s, full detail 64 m %s, all %s",
        params.workers, params.uploadsPerFrame, seconds2(lastShown(selection, today, 2)).c_str(),
        seconds2(lastShown(selection, today, 4)).c_str(),
        seconds2(lastShown(selection, today, 16)).c_str(),
        seconds2(lastShown(selection, today, -1)).c_str(),
        seconds2(lastShown(coarse, coarseShown, 2)).c_str(),
        seconds2(lastShown(coarse, coarseShown, 4)).c_str(),
        seconds2(lastShown(coarse, coarseShown, 16)).c_str(),
        seconds2(lastShown(coarse, coarseShown, -1)).c_str(),
        seconds2(lastShown(selection, selectionShown, 4)).c_str(),
        seconds2(lastShown(selection, selectionShown, -1)).c_str());
    return text;
}

} // namespace

std::vector<double> simulate(std::span<const double> costs, const Params& params) {
    if (params.workers < 1 || params.uploadsPerFrame < 1 || !(params.frameSeconds > 0.0) ||
        std::any_of(costs.begin(), costs.end(), [](double cost) { return cost < 0.0; })) {
        throw std::invalid_argument("load_schedule::simulate: invalid parameters");
    }
    std::vector<double> free(static_cast<std::size_t>(params.workers), 0.0);
    std::vector<double> generated(costs.size());
    for (std::size_t i = 0; i < costs.size(); ++i) {
        const auto worker = std::min_element(free.begin(), free.end());
        *worker += costs[i];
        generated[i] = *worker;
    }
    std::vector<double> shown(costs.size(), -1.0);
    std::size_t remaining = costs.size();
    for (std::size_t update = 0; remaining > 0; ++update) {
        const double now = static_cast<double>(update) * params.frameSeconds;
        std::size_t taken = 0;
        for (std::size_t i = 0; i < costs.size() && taken < params.uploadsPerFrame; ++i) {
            if (shown[i] < 0.0 && generated[i] <= now) {
                shown[i] = now;
                ++taken;
            }
        }
        remaining -= taken;
    }
    return shown;
}

double lastShown(std::span<const Tile> tiles, std::span<const double> shown, int reach) {
    double last = -1.0;
    for (std::size_t i = 0; i < tiles.size() && i < shown.size(); ++i) {
        if (reach < 0 || tiles[i].distanceChunks < reach) {
            last = std::max(last, shown[i]);
        }
    }
    return last;
}

std::vector<std::string> report(const terrain::TerrainTiles& tiles,
                                const terrain::StreamParams& params, std::span<const Place> places) {
    std::vector<std::string> lines;
    for (const Place& place : places) {
        const terrain::ChunkCoord chunk = terrain::worldToChunk(place.x, place.z);
        Costs costs;
        const std::vector<Tile> selection = measured(tiles, chunk, params.lod, costs);
        const terrain::LodParams coarseLod{kCoarseSplit, params.lod.viewDistance};
        const std::vector<Tile> coarse = measured(tiles, chunk, coarseLod, costs);
        std::vector<std::size_t> index;
        const std::vector<Tile> order = coarseOrder(coarse, selection, index);
        char text[160];
        std::snprintf(text, sizeof(text), "schedule at (%.1f, %.1f) m:", place.x, place.z);
        lines.emplace_back(text);
        lines.push_back(describeSelection("selection", selection));
        lines.push_back(describeSelection("coarse stage", coarse));
        lines.push_back(describeSelection("coarse stage then the rest", order));
        for (const int workers : {2, 4, 8, 16}) {
            for (const std::size_t uploads : {std::size_t{8}, std::size_t{32}}) {
                lines.push_back(simulateCase(selection, coarse, {workers, uploads}));
            }
        }
    }
    return lines;
}

} // namespace load_schedule
