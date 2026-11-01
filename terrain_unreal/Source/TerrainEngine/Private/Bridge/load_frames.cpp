// The frames of a load (load_timeline.hpp): their CSV, the worst of them and its parts.
#include "load_timeline.hpp"

#include <algorithm>
#include <cstdio>

namespace terrain::unreal {

namespace {

// The length of a frame: on the game thread when known, of the terrain otherwise.
double lengthOf(const FrameTrace& frame) {
    return std::max(frame.gameThreadMilliseconds, frame.terrainMilliseconds);
}

} // namespace

FrameTrace worstFrame(std::span<const FrameTrace> frames) {
    const auto worst = std::max_element(
        frames.begin(), frames.end(),
        [](const FrameTrace& a, const FrameTrace& b) { return lengthOf(a) < lengthOf(b); });
    return worst == frames.end() ? FrameTrace{} : *worst;
}

std::string describeFrame(const FrameTrace& frame) {
    char text[256];
    std::snprintf(text, sizeof(text),
                  "%.1f ms at %.3f s (terrain %.1f ms: tiles %.1f, %zu uploads %.1f, horizon %.1f, "
                  "textures %.1f; %d levels)",
                  lengthOf(frame), frame.seconds, frame.terrainMilliseconds,
                  frame.tilesMilliseconds, frame.uploads, frame.uploadsMilliseconds,
                  frame.horizonMilliseconds, frame.texturesMilliseconds, frame.horizonLevels);
    return text;
}

std::vector<std::string> frameRows(std::span<const FrameTrace> frames) {
    std::vector<std::string> rows;
    rows.reserve(frames.size() + 1);
    rows.emplace_back("t_s,game_thread_ms,terrain_ms,tiles_ms,uploads,uploads_ms,horizon_ms,"
                      "textures_ms,horizon_levels");
    char text[192];
    for (const FrameTrace& frame : frames) {
        std::snprintf(text, sizeof(text), "%.4f,%.3f,%.3f,%.3f,%zu,%.3f,%.3f,%.3f,%d",
                      frame.seconds, frame.gameThreadMilliseconds, frame.terrainMilliseconds,
                      frame.tilesMilliseconds, frame.uploads, frame.uploadsMilliseconds,
                      frame.horizonMilliseconds, frame.texturesMilliseconds, frame.horizonLevels);
        rows.emplace_back(text);
    }
    return rows;
}

} // namespace terrain::unreal
