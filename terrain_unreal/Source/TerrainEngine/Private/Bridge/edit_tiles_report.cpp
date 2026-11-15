#include "edit_tiles_report.hpp"

#include "tile_column.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace terrain::unreal {

namespace {

std::string format(const char* pattern, double a, double b) {
    char text[96];
    std::snprintf(text, sizeof(text), pattern, a, b);
    return text;
}

std::string pointText(const CorePoint& p) {
    char text[96];
    std::snprintf(text, sizeof(text), "(%.2f, %.2f, %.2f)", p.x, p.y, p.z);
    return text;
}

std::string chunkText(const CorePoint& p) {
    const ChunkCoord chunk = worldToChunk(p.x, p.z);
    char text[64];
    std::snprintf(text, sizeof(text), "chunk (%d, %d)", chunk.x, chunk.z);
    return text;
}

double distance(const CorePoint& a, const CorePoint& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

// "6.19 m", "6.19 m, under it 5.50 3.50 m", or "none".
std::string heightsText(const std::vector<double>& heights) {
    if (heights.empty()) {
        return "none";
    }
    std::string text = format("%.2f m", heights.front(), 0.0);
    if (heights.size() > 1) {
        text += ", under it";
        for (std::size_t i = 1; i < heights.size(); ++i) {
            text += format(" %.2f", heights[i], 0.0);
        }
        text += " m";
    }
    return text;
}

// The ground of the core and of the mesh differ by more than this (the mesh in float
// centimeters): the mesh is not the tile of the core.
constexpr double kSameTopMeters = 0.01;

bool differ(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        return true;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > kSameTopMeters) {
            return true;
        }
    }
    return false;
}

std::optional<double> highest(const std::vector<double>& heights) {
    return heights.empty() ? std::nullopt : std::optional<double>(heights.front());
}

} // namespace

std::string positionsLine(const UnrealFrame& frame, double x, double z, const ViewPositions& view) {
    const CorePoint focus = toCore(frame, view.focus);
    const CorePoint camera = toCore(frame, view.camera);
    std::string line = format("EditTiles (%.2f, %.2f): streaming", x, z);
    line += view.focusActor ? " (FocusActor)" : " (the pawn possessed)";
    line += " at " + pointText(focus) + " " + chunkText(focus) + ", camera at " +
            pointText(camera) + " " + chunkText(camera) +
            format(" %.1f m from it", distance(focus, camera), 0.0);
    if (view.pawn) {
        const CorePoint pawn = toCore(frame, *view.pawn);
        line += ", pawn " + view.pawnClass + " at " + pointText(pawn) + " " + chunkText(pawn) +
                format(" %.1f m from the camera", distance(pawn, camera), 0.0);
    } else {
        line += ", no pawn";
    }
    return line;
}

bool tileHolds(const TileKey& image, double x, double z, double margin) {
    const double side = tileSizeMeters(image.lod);
    const double x0 = image.origin.x * kChunkSizeMeters;
    const double z0 = image.origin.z * kChunkSizeMeters;
    return x >= x0 - margin && x <= x0 + side + margin && z >= z0 - margin &&
           z <= z0 + side + margin;
}

std::vector<TileKey> tilesOver(const TileStreamer& streamer, double x, double z, double margin) {
    std::vector<TileKey> keys;
    for (const TileKey& key : streamer.manager().loadedTiles()) {
        if (streamer.shown(key) && tileHolds(streamer.shownImage(key), x, z, margin)) {
            keys.push_back(key);
        }
    }
    return keys;
}

int expectedLodAt(const LodParams& params, ChunkCoord camera, double x, double z) {
    const ChunkCoord chunk = worldToChunk(x, z);
    for (const TileKey& key : selectTiles(camera, params)) {
        const int side = tileChunks(key.lod);
        if (chunk.x >= key.origin.x && chunk.x < key.origin.x + side && chunk.z >= key.origin.z &&
            chunk.z < key.origin.z + side) {
            return key.lod;
        }
    }
    return -1;
}

TileColumnReport reportTile(const TileStreamer& streamer, const UnrealFrame& frame,
                            const TileKey& key, double x, double z, ChunkCoord camera,
                            const std::optional<ShownSection>& shown) {
    TileColumnReport report;
    report.key = key;
    report.image = streamer.shownImage(key);
    report.expectedLod = expectedLodAt(streamer.manager().params().lod, camera, x, z);
    const TileData* data = streamer.shown(key) ? streamer.manager().find(key) : nullptr;
    report.inStreamer = data != nullptr;
    if (data != nullptr) {
        // The data lie at the canonical image (whole worlds from the image shown).
        const double dx = (key.origin.x - report.image.origin.x) * kChunkSizeMeters;
        const double dz = (key.origin.z - report.image.origin.z) * kChunkSizeMeters;
        report.coreHeights =
            surfaceHeights(coreVertices(*data), data->mesh.indices, x + dx, z + dz);
        report.coreTop = highest(report.coreHeights);
        report.coreVertices = data->mesh.vertices.size();
        report.editedVertices = static_cast<std::size_t>(std::count_if(
            data->editWeights.begin(), data->editWeights.end(), [](float w) { return w > 0.0f; }));
    }
    if (!shown) {
        return report;
    }
    report.inMesh = true;
    std::vector<CorePoint> vertices;
    vertices.reserve(shown->positions.size());
    for (const UnrealPoint& p : shown->positions) {
        vertices.push_back(toCore(frame, p));
    }
    report.shownHeights = surfaceHeights(vertices, shown->triangles, x, z);
    report.shownTop = highest(report.shownHeights);
    report.shownVertices = shown->positions.size();
    const CorePoint a = toCore(frame, shown->boundsMin);
    const CorePoint b = toCore(frame, shown->boundsMax);
    report.boundsLow = {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    report.boundsHigh = {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    report.pointInBounds = x >= report.boundsLow.x && x <= report.boundsHigh.x &&
                           z >= report.boundsLow.z && z <= report.boundsHigh.z;
    report.visible = shown->visible;
    report.sectionVisible = shown->sectionVisible;
    report.uploadedSecondsAgo = shown->uploadedSecondsAgo;
    report.uploads = shown->uploads;
    return report;
}

std::string editTilesLine(double x, double z, const TileColumnReport& r) {
    std::string line = format("EditTiles (%.2f, %.2f): tile ", x, z);
    char text[160];
    std::snprintf(text, sizeof(text), "(%d, %d) LOD %d (from the camera: LOD %d)", r.key.origin.x,
                  r.key.origin.z, r.key.lod, r.expectedLod);
    line += text;
    if (r.image != r.key) {
        std::snprintf(text, sizeof(text), " [image (%d, %d)]", r.image.origin.x, r.image.origin.z);
        line += text;
    }
    if (r.inStreamer) {
        std::snprintf(text, sizeof(text), " (%zu vertices, %zu edited)", r.coreVertices,
                      r.editedVertices);
        line += ": core top " + heightsText(r.coreHeights) + text;
    } else {
        line += ": not shown by the streamer";
    }
    if (!r.inMesh) {
        return line + " | no mesh in Unreal";
    }
    std::snprintf(text, sizeof(text), " (%zu vertices, uploaded %.1f s ago, upload %d)",
                  r.shownVertices, r.uploadedSecondsAgo, r.uploads);
    line += " | shown top " + heightsText(r.shownHeights) + text;
    std::snprintf(text, sizeof(text), ", bounds x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f",
                  r.boundsLow.x, r.boundsHigh.x, r.boundsLow.y, r.boundsHigh.y, r.boundsLow.z,
                  r.boundsHigh.z);
    line += text;
    line += r.pointInBounds ? " (the point inside)" : " (the point OUTSIDE)";
    line += r.visible && r.sectionVisible ? ", visible" : ", HIDDEN";
    if (r.inStreamer && differ(r.coreHeights, r.shownHeights)) {
        line += ", DIFFERENT from the core";
    }
    return line;
}

} // namespace terrain::unreal
