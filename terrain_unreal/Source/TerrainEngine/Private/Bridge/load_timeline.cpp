#include "load_timeline.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terrain::unreal {

namespace {

constexpr std::array<const char*, kLoadMilestoneCount> kMilestoneNames{
    "ground", "ground collision", "64 m", "256 m", "all tiles", "horizon", "horizon detail"};

constexpr double kNotYet = -1.0;

std::size_t bandOf(int distanceChunks) {
    const double meters = distanceChunks * kChunkSizeMeters;
    std::size_t band = 0;
    for (std::size_t i = 0; i < kLoadBandStartMeters.size(); ++i) {
        if (meters >= kLoadBandStartMeters[i]) {
            band = i;
        }
    }
    return band;
}

std::string bandName(std::size_t band) {
    const int first = kLoadBandStartMeters[band];
    if (band + 1 == kLoadBandStartMeters.size()) {
        return std::to_string(first) + "+ m";
    }
    return std::to_string(first) + "-" + std::to_string(kLoadBandStartMeters[band + 1]) + " m";
}

std::string seconds3(double seconds) {
    if (seconds < 0.0) {
        return "not yet";
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f s", seconds);
    return text;
}

bool nearerTrace(const TileTrace& a, const TileTrace& b) {
    return std::tuple(a.distanceChunks, a.key.lod, a.key.origin.z, a.key.origin.x) <
           std::tuple(b.distanceChunks, b.key.lod, b.key.origin.z, b.key.origin.x);
}

// The reach of a tile milestone: the selected tiles closer than this many chunks (all: -1).
int milestoneReach(LoadMilestone milestone) {
    switch (milestone) {
    case LoadMilestone::Ground:
        return kGroundChunks + 1;
    case LoadMilestone::Ring64:
        return kRing64Chunks;
    case LoadMilestone::Ring256:
        return kRing256Chunks;
    default:
        return -1;
    }
}

} // namespace

const char* loadReasonName(LoadReason reason) {
    switch (reason) {
    case LoadReason::Spawn:
        return "spawn";
    case LoadReason::Teleport:
        return "teleport";
    case LoadReason::Prepared:
        return "prepared point";
    }
    return "?";
}

const char* milestoneName(LoadMilestone milestone) {
    return kMilestoneNames[static_cast<std::size_t>(milestone)];
}

std::vector<SelectedTile> selectedTiles(const TileStreamer& streamer, ChunkCoord player) {
    const StreamManager& manager = streamer.manager();
    std::vector<SelectedTile> tiles;
    tiles.reserve(manager.selection().size());
    for (const TileRequest& request : manager.selection()) {
        const TileKey image = nearestTile(request.key, player, manager.params().world);
        tiles.push_back({request.key, tileDistance(image, player), streamer.shown(request.key)});
    }
    return tiles;
}

bool horizonAround(const HorizonClipmap& clipmap, double x, double z) {
    return clipmap.placed() && clipmap.level(0).heightAt(x, z).has_value();
}

int wrappedChunkDistance(ChunkCoord a, ChunkCoord b, const WorldSize& world) {
    const ChunkCoord count = worldChunks(world);
    const auto axis = [](std::int64_t from, std::int64_t to, std::int64_t period) {
        const std::int64_t gap = wrapIndex(to - from, period); // centered on 0
        return gap < 0 ? -gap : gap;
    };
    return static_cast<int>(std::max(axis(a.x, b.x, count.x), axis(a.z, b.z, count.z)));
}

void LoadTimeline::begin(LoadReason reason, double playerX, double playerZ,
                         LoadClock::time_point now, const LoadExpectations& expectations) {
    const std::lock_guard lock(mutex_);
    active_ = true;
    summary_ = {};
    summary_.reason = reason;
    summary_.playerX = playerX;
    summary_.playerZ = playerZ;
    summary_.milestones.fill(kNotYet);
    expectations_ = expectations;
    start_ = now;
    traces_.clear();
    threads_.clear();
}

bool LoadTimeline::active() const {
    const std::lock_guard lock(mutex_);
    return active_;
}

bool LoadTimeline::complete() const {
    const std::lock_guard lock(mutex_);
    return completeLocked();
}

void LoadTimeline::recordGeneration(const TileKey& key, LoadClock::time_point start,
                                    LoadClock::time_point end, std::thread::id thread) {
    const std::lock_guard lock(mutex_);
    if (!active_) {
        return;
    }
    TileTrace& trace = traces_[key];
    trace.key = key;
    trace.thread = threadIndex(thread);
    trace.generationStart = seconds(start);
    trace.generationEnd = seconds(end);
    ++trace.generations;
}

void LoadTimeline::recordConverted(const TileKey& key, LoadClock::time_point now) {
    const std::lock_guard lock(mutex_);
    if (active_) {
        TileTrace& trace = traces_[key];
        trace.key = key;
        trace.converted = seconds(now);
    }
}

void LoadTimeline::recordShown(const TileKey& key, LoadClock::time_point now,
                               double uploadMilliseconds, bool collision) {
    const std::lock_guard lock(mutex_);
    if (!active_) {
        return;
    }
    TileTrace& trace = traces_[key];
    trace.key = key;
    trace.shown = seconds(now);
    trace.uploadMilliseconds = uploadMilliseconds;
    trace.collision = collision;
    ++summary_.uploads;
    summary_.uploadMilliseconds += uploadMilliseconds;
}

void LoadTimeline::recordFrame(double gameThreadMilliseconds, std::size_t uploads) {
    const std::lock_guard lock(mutex_);
    if (!active_) {
        return;
    }
    ++summary_.frames;
    summary_.worstFrameMilliseconds =
        std::max(summary_.worstFrameMilliseconds, gameThreadMilliseconds);
    summary_.mostUploadsPerFrame = std::max(summary_.mostUploadsPerFrame, uploads);
}

std::vector<LoadMilestone> LoadTimeline::observe(std::span<const SelectedTile> selection,
                                                 LoadClock::time_point now) {
    const std::lock_guard lock(mutex_);
    std::vector<LoadMilestone> reached;
    if (!active_) {
        return reached;
    }
    // The nearest tile not shown yet bounds the rings complete now.
    int nearestMissing = -1;
    for (const SelectedTile& tile : selection) {
        TileTrace& trace = traces_[tile.key];
        trace.key = tile.key;
        trace.distanceChunks = tile.distanceChunks;
        if (!tile.shown && (nearestMissing < 0 || tile.distanceChunks < nearestMissing)) {
            nearestMissing = tile.distanceChunks;
        }
    }
    const double at = seconds(now);
    for (const LoadMilestone milestone : {LoadMilestone::Ground, LoadMilestone::Ring64,
                                          LoadMilestone::Ring256, LoadMilestone::AllTiles}) {
        const int reach = milestoneReach(milestone);
        const bool done = nearestMissing < 0 || (reach >= 0 && nearestMissing >= reach);
        if (!selection.empty() && done && reachMilestone(milestone, at)) {
            reached.push_back(milestone);
        }
    }
    active_ = !completeLocked();
    return reached;
}

bool LoadTimeline::mark(LoadMilestone milestone, LoadClock::time_point now) {
    const std::lock_guard lock(mutex_);
    if (!active_) {
        return false;
    }
    const bool first = reachMilestone(milestone, seconds(now));
    active_ = !completeLocked();
    return first;
}

bool LoadTimeline::reached(LoadMilestone milestone) const {
    const std::lock_guard lock(mutex_);
    return summary_.milestones[static_cast<std::size_t>(milestone)] >= 0.0;
}

double LoadTimeline::secondsSince(LoadClock::time_point now) const {
    const std::lock_guard lock(mutex_);
    return seconds(now);
}

std::vector<LoadMilestone> LoadTimeline::missing() const {
    const std::lock_guard lock(mutex_);
    std::vector<LoadMilestone> result;
    for (std::size_t i = 0; i < kLoadMilestoneCount; ++i) {
        const auto milestone = static_cast<LoadMilestone>(i);
        if (expected(milestone) && summary_.milestones[i] < 0.0) {
            result.push_back(milestone);
        }
    }
    return result;
}

double LoadTimeline::seconds(LoadClock::time_point time) const {
    const std::chrono::duration<double> elapsed = time - start_;
    return elapsed.count();
}

bool LoadTimeline::expected(LoadMilestone milestone) const {
    switch (milestone) {
    case LoadMilestone::GroundCollision:
        return expectations_.groundCollision;
    case LoadMilestone::Horizon:
        return expectations_.horizon;
    case LoadMilestone::HorizonDetail:
        return expectations_.horizon && expectations_.horizonDetail;
    default:
        return true;
    }
}

bool LoadTimeline::completeLocked() const {
    for (std::size_t i = 0; i < kLoadMilestoneCount; ++i) {
        if (expected(static_cast<LoadMilestone>(i)) && summary_.milestones[i] < 0.0) {
            return false;
        }
    }
    return true;
}

bool LoadTimeline::reachMilestone(LoadMilestone milestone, double at) {
    double& slot = summary_.milestones[static_cast<std::size_t>(milestone)];
    if (slot >= 0.0) {
        return false;
    }
    slot = std::max(at, 0.0);
    return true;
}

int LoadTimeline::threadIndex(std::thread::id thread) {
    const auto [it, inserted] = threads_.try_emplace(thread, static_cast<int>(threads_.size()));
    return it->second;
}

LoadSummary LoadTimeline::summary() const {
    const std::lock_guard lock(mutex_);
    LoadSummary result = summary_;
    result.tiles = traces_.size();
    result.bandShown.fill(0.0);
    std::array<double, kLoadBandStartMeters.size()> generationMs{};
    std::array<std::size_t, kLoadBandStartMeters.size()> generated{};
    result.threadBusySeconds.assign(threads_.size(), 0.0);
    result.threadTiles.assign(threads_.size(), 0);
    for (const auto& [key, trace] : traces_) {
        result.generatedAgain += trace.generations > 1 ? 1 : 0;
        if (trace.thread >= 0 && trace.generationEnd >= trace.generationStart) {
            const auto thread = static_cast<std::size_t>(trace.thread);
            result.threadBusySeconds[thread] += trace.generationEnd - trace.generationStart;
            ++result.threadTiles[thread];
        }
        if (trace.distanceChunks < 0) {
            continue; // never selected: generated for another place
        }
        const std::size_t band = bandOf(trace.distanceChunks);
        ++result.bandTiles[band];
        double& shown = result.bandShown[band];
        shown = (shown < 0.0 || trace.shown < 0.0) ? kNotYet : std::max(shown, trace.shown);
        if (trace.generations > 0) {
            generationMs[band] += (trace.generationEnd - trace.generationStart) * 1000.0;
            ++generated[band];
        }
    }
    for (std::size_t band = 0; band < kLoadBandStartMeters.size(); ++band) {
        result.bandShown[band] = result.bandTiles[band] == 0 ? kNotYet : result.bandShown[band];
        result.bandGenerationMs[band] =
            generated[band] == 0 ? 0.0 : generationMs[band] / static_cast<double>(generated[band]);
    }
    return result;
}

std::vector<TileTrace> LoadTimeline::traces() const {
    std::vector<TileTrace> result;
    {
        const std::lock_guard lock(mutex_);
        result.reserve(traces_.size());
        for (const auto& [key, trace] : traces_) {
            result.push_back(trace);
        }
    }
    std::sort(result.begin(), result.end(), nearerTrace);
    return result;
}

TileGenerator tracedGenerator(TileGenerator generator, std::shared_ptr<LoadTimeline> timeline) {
    if (!generator || timeline == nullptr) {
        throw std::invalid_argument("tracedGenerator: a generator and a timeline expected");
    }
    return [generator = std::move(generator),
            timeline = std::move(timeline)](const TileRequest& request) {
        const LoadClock::time_point start = LoadClock::now();
        TileData data = generator(request);
        timeline->recordGeneration(request.key, start, LoadClock::now(),
                                   std::this_thread::get_id());
        return data;
    };
}

std::string describeMilestone(LoadMilestone milestone, double seconds) {
    return std::string(milestoneName(milestone)) + " " + seconds3(seconds);
}

std::vector<std::string> describe(const LoadSummary& summary) {
    std::vector<std::string> lines;
    char text[256];
    std::snprintf(text, sizeof(text), "loading (%s) at (%.1f, %.1f) m: %zu tiles traced",
                  loadReasonName(summary.reason), summary.playerX, summary.playerZ, summary.tiles);
    lines.emplace_back(text);
    std::string milestones = "milestones:";
    for (std::size_t i = 0; i < kLoadMilestoneCount; ++i) {
        milestones += (i == 0 ? " " : ", ") +
                      describeMilestone(static_cast<LoadMilestone>(i), summary.milestones[i]);
    }
    lines.push_back(milestones);
    for (std::size_t band = 0; band < kLoadBandStartMeters.size(); ++band) {
        if (summary.bandTiles[band] == 0) {
            continue;
        }
        std::snprintf(text, sizeof(text), "distance %s: %zu tiles, all shown at %s, generation "
                      "%.2f ms per tile",
                      bandName(band).c_str(), summary.bandTiles[band],
                      seconds3(summary.bandShown[band]).c_str(), summary.bandGenerationMs[band]);
        lines.emplace_back(text);
    }
    for (std::size_t thread = 0; thread < summary.threadTiles.size(); ++thread) {
        std::snprintf(text, sizeof(text), "generation thread %zu: %zu tiles, busy %.3f s", thread,
                      summary.threadTiles[thread], summary.threadBusySeconds[thread]);
        lines.emplace_back(text);
    }
    const double meanUpload = summary.uploads == 0
                                  ? 0.0
                                  : summary.uploadMilliseconds / static_cast<double>(summary.uploads);
    std::snprintf(text, sizeof(text), "%zu uploads %.2f ms each, %zu frames, worst game thread "
                  "%.1f ms, at most %zu uploads in a frame, %zu tiles generated again",
                  summary.uploads, meanUpload, summary.frames, summary.worstFrameMilliseconds,
                  summary.mostUploadsPerFrame, summary.generatedAgain);
    lines.emplace_back(text);
    return lines;
}

std::vector<std::string> csvRows(std::span<const TileTrace> traces) {
    std::vector<std::string> rows;
    rows.reserve(traces.size() + 1);
    rows.emplace_back("chunk_x,chunk_z,lod,distance_m,thread,generations,generation_start_s,"
                      "generation_end_s,generation_ms,converted_s,shown_s,upload_ms,collision");
    char text[256];
    for (const TileTrace& trace : traces) {
        const double generationMs =
            trace.generations > 0 ? (trace.generationEnd - trace.generationStart) * 1000.0 : 0.0;
        std::snprintf(text, sizeof(text), "%d,%d,%d,%d,%d,%d,%.4f,%.4f,%.3f,%.4f,%.4f,%.3f,%d",
                      trace.key.origin.x, trace.key.origin.z, trace.key.lod,
                      trace.distanceChunks < 0
                          ? -1
                          : static_cast<int>(trace.distanceChunks * kChunkSizeMeters),
                      trace.thread, trace.generations, trace.generationStart, trace.generationEnd,
                      generationMs, trace.converted, trace.shown, trace.uploadMilliseconds,
                      trace.collision ? 1 : 0);
        rows.emplace_back(text);
    }
    return rows;
}

} // namespace terrain::unreal
