#include "dev_perf.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace terrain::unreal {

namespace {

MeanMax meanMax(const std::deque<double>& values) {
    MeanMax out;
    if (values.empty()) {
        return out;
    }
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
        out.max = std::max(out.max, v);
    }
    out.mean = sum / static_cast<double>(values.size());
    return out;
}

// The mean time of a stage over the window: its time over its tiles, between two totals.
double meanBetween(const StageStats& first, const StageStats& last) {
    const std::size_t tiles = last.tiles - first.tiles;
    return tiles == 0 ? 0.0
                      : (last.totalMilliseconds - first.totalMilliseconds) /
                            static_cast<double>(tiles);
}

std::string line(const char* pattern, double a, double b) {
    char text[160];
    std::snprintf(text, sizeof(text), pattern, a, b);
    return text;
}

} // namespace

PerfWindow::PerfWindow(double windowSeconds) : window_(windowSeconds) {
    if (!(windowSeconds > 0.0)) {
        throw std::invalid_argument("PerfWindow: the window must last more than 0 s");
    }
}

void PerfWindow::addFrame(double now, const FrameTimes& times) {
    frames_.push_back({now, times});
    while (!frames_.empty() && frames_.front().time < now - window_) {
        frames_.pop_front();
    }
}

void PerfWindow::addTiles(double now, const TimingsReport& totals) {
    tiles_.push_back({now, totals});
    // The oldest totals kept is the last one at or before the start of the window.
    while (tiles_.size() > 1 && tiles_[1].time <= now - window_) {
        tiles_.pop_front();
    }
}

void PerfWindow::clear() {
    frames_.clear();
    tiles_.clear();
}

PerfSummary PerfWindow::summary() const {
    PerfSummary out;
    out.window = window_;
    std::deque<double> frame;
    std::deque<double> game;
    std::deque<double> render;
    std::deque<double> gpu;
    for (const Frame& f : frames_) {
        frame.push_back(f.times.frame);
        game.push_back(f.times.game);
        render.push_back(f.times.render);
        gpu.push_back(f.times.gpu);
    }
    out.frame = meanMax(frame);
    out.game = meanMax(game);
    out.render = meanMax(render);
    out.gpu = meanMax(gpu);
    out.framesPerSecond = out.frame.mean > 0.0 ? 1000.0 / out.frame.mean : 0.0;
    if (tiles_.size() < 2) {
        return out;
    }
    const Tiles& first = tiles_.front();
    const Tiles& last = tiles_.back();
    out.seconds = last.time - first.time;
    for (std::size_t lod = 0; lod < kLodCount; ++lod) {
        const LodTimings& a = first.totals[lod];
        const LodTimings& b = last.totals[lod];
        const StageStats& generated = b.stage(TileStage::Generation);
        const double tiles =
            static_cast<double>(generated.tiles - a.stage(TileStage::Generation).tiles);
        out.tilesPerSecond[lod] = out.seconds > 0.0 ? tiles / out.seconds : 0.0;
        out.generationMs[lod] = meanBetween(a.stage(TileStage::Generation), generated);
        out.conversionMs[lod] =
            meanBetween(a.stage(TileStage::Conversion), b.stage(TileStage::Conversion));
        out.uploadMs[lod] = meanBetween(a.stage(TileStage::Upload), b.stage(TileStage::Upload));
        out.waterTiles += b.water.tiles - a.water.tiles;
        out.waterMs += b.water.totalMilliseconds - a.water.totalMilliseconds;
    }
    out.waterMs = out.waterTiles == 0 ? 0.0 : out.waterMs / static_cast<double>(out.waterTiles);
    return out;
}

std::vector<std::string> describe(const PerfSummary& s) {
    std::vector<std::string> out;
    out.push_back(line("fps %.1f, frame %.2f ms", s.framesPerSecond, s.frame.mean) +
                  line(" (max %.2f), game thread %.2f ms", s.frame.max, s.game.mean));
    out.push_back(line("render thread %.2f ms, GPU %.2f ms", s.render.mean, s.gpu.mean) +
                  line(" (max %.2f / %.2f)", s.render.max, s.gpu.max));
    for (std::size_t lod = 0; lod < kLodCount; ++lod) {
        out.push_back("LOD " + std::to_string(lod) +
                      line(": %.1f tiles/s, generation %.2f ms", s.tilesPerSecond[lod],
                           s.generationMs[lod]) +
                      line(", conversion %.2f ms, upload %.2f ms", s.conversionMs[lod],
                           s.uploadMs[lod]));
    }
    out.push_back(line("water meshes: %.0f tiles, %.2f ms each",
                       static_cast<double>(s.waterTiles), s.waterMs) +
                  line(", tiles over the last %.1f s (window %.0f s)", s.seconds, s.window));
    return out;
}

} // namespace terrain::unreal
