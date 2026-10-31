// Measures of the load of the world around the player (agent 38), run by hand (not a test): the
// streaming of the Unreal plugin (TerrainSession, TileStreamer) and its GPU far horizon
// (HorizonGpuStreamer, jobs on a pool of threads as Unreal runs them on its tasks), in frames of
// 60 Hz as the game ticks, at a spawn point and after each teleport. For each load, the timeline
// (Bridge/load_timeline.hpp): the milestones, the curve "distance of the tile -> instant shown",
// the threads and the frames; optionally the CSV of the tiles. The meshes of Unreal are not built
// here: a tile counts as shown once converted (Unreal adds its upload, logged in the game).
//
//   unreal_loading_bench [--planet <file>] [--at <x> <z>] [--teleport <x> <z>]... [--workers <n>]
//                        [--pieces <n>] [--uploads <n>] [--no-horizon] [--schedule]
//                        [--csv <prefix>] [--timeout <seconds>] [--levels]
//
// --levels: the cost of the first epoch of the horizon around the spawn, level by level, on one
// thread (a few pieces of each level timed, the level extrapolated), as the GPU horizon computes
// it (point samples, the crests deferred); then the bench ends.
// --schedule: after the loads, the cost of every tile of the selection around each place,
// measured one at a time, then the load simulated for 2 to 16 workers and two orders: the order of
// today (nearest first) and a coarse stage first (rings {2, 4, 8}, then the final rings).
#include "load_schedule.hpp"
#include "load_timeline.hpp"
#include "terrain_session.hpp"

#include "horizon_gpu_streamer.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::unreal::LoadClock;
using terrain::unreal::LoadMilestone;
using terrain::unreal::LoadReason;
using terrain::unreal::LoadTimeline;

struct Place {
    double x = 0.0;
    double z = 0.0;
};

struct Options {
    std::string planet;
    Place spawn;
    std::vector<Place> teleports;
    int workers = 2;
    int pieces = 2;
    int uploads = 8;
    bool horizon = true;
    bool schedule = false;
    bool levels = false;
    std::string csv;
    double timeoutSeconds = 90.0;
};

double number(int argc, char** argv, int& i) {
    if (i + 1 >= argc) {
        throw std::invalid_argument(std::string("a value expected after ") + argv[i]);
    }
    char* end = nullptr;
    const double value = std::strtod(argv[++i], &end);
    if (end == argv[i] || *end != '\0') {
        throw std::invalid_argument(std::string("not a number: ") + argv[i]);
    }
    return value;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--planet" && i + 1 < argc) {
            options.planet = argv[++i];
        } else if (arg == "--at") {
            options.spawn.x = number(argc, argv, i);
            options.spawn.z = number(argc, argv, i);
        } else if (arg == "--teleport") {
            Place place;
            place.x = number(argc, argv, i);
            place.z = number(argc, argv, i);
            options.teleports.push_back(place);
        } else if (arg == "--workers") {
            options.workers = static_cast<int>(number(argc, argv, i));
        } else if (arg == "--pieces") {
            options.pieces = static_cast<int>(number(argc, argv, i));
        } else if (arg == "--uploads") {
            options.uploads = static_cast<int>(number(argc, argv, i));
        } else if (arg == "--timeout") {
            options.timeoutSeconds = number(argc, argv, i);
        } else if (arg == "--no-horizon") {
            options.horizon = false;
        } else if (arg == "--schedule") {
            options.schedule = true;
        } else if (arg == "--levels") {
            options.levels = true;
        } else if (arg == "--csv" && i + 1 < argc) {
            options.csv = argv[++i];
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }
    if (options.workers < 1 || options.pieces < 1 || options.uploads < 1) {
        throw std::invalid_argument("workers, pieces and uploads must be at least 1");
    }
    return options;
}

// The jobs of the horizon on `threads` threads, first come first served.
class Pool {
public:
    explicit Pool(int threads) {
        for (int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { work(); });
        }
    }
    ~Pool() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        workers_.clear();
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    [[nodiscard]] terrain::unreal::JobLauncher launcher() {
        return [this](std::function<void()> job) {
            {
                const std::lock_guard lock(mutex_);
                jobs_.push_back(std::move(job));
            }
            ready_.notify_one();
        };
    }

private:
    void work() {
        std::unique_lock lock(mutex_);
        while (true) {
            ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                return;
            }
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop_front();
            lock.unlock();
            job();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::vector<std::jthread> workers_; // last: joined first
};

double secondsSince(LoadClock::time_point start) {
    return std::chrono::duration<double>(LoadClock::now() - start).count();
}

// The world of the game: the session, the frame of the spawn and the GPU horizon.
struct Game {
    terrain::unreal::TerrainSession session;
    terrain::unreal::UnrealFrame frame;
    Pool pool;
    std::unique_ptr<terrain::unreal::HorizonGpuStreamer> horizon;

    Game(const terrain::unreal::SessionParams& params, const Options& options)
        : session(params), frame(session.frameAt(options.spawn.x, options.spawn.z)),
          pool(options.pieces) {}
};

terrain::unreal::SessionParams sessionParams(const Options& options) {
    terrain::unreal::SessionParams params;
    params.planetFile = options.planet;
    params.stream.workerThreads = options.workers;
    params.stream.maxLoadsPerUpdate = static_cast<std::size_t>(options.uploads);
    return params;
}

void startHorizon(Game& game, const Options& options) {
    const LoadClock::time_point start = LoadClock::now();
    terrain::unreal::HorizonGpuParams params;
    params.maxPiecesInFlight = options.pieces;
    params.lod = game.session.streamer().manager().params().lod;
    game.horizon = std::make_unique<terrain::unreal::HorizonGpuStreamer>(
        game.session.tiles().heightfield(), game.session.tiles().ecology(),
        game.session.tiles().world(), params, game.pool.launcher(), &game.session.horizonEdits());
    std::printf("horizon: %d levels, %d pieces at once, built in %.3f s\n", params.levels,
                options.pieces, secondsSince(start));
}

// The worst updates of the tiles and of the horizon in a load, milliseconds.
struct Worst {
    double tiles = 0.0;
    double horizon = 0.0;
};

// One frame of the game: the tiles (their conversions recorded), the horizon, the milestones.
void frame(Game& game, const Place& place, const terrain::unreal::UnrealPoint& position,
           Worst& worst) {
    LoadTimeline& timeline = game.session.timeline();
    const LoadClock::time_point start = LoadClock::now();
    const terrain::unreal::StreamerUpdate update =
        game.session.streamer().update(position, game.frame);
    for (const terrain::unreal::TileUpload& upload : update.uploads) {
        timeline.recordShown(upload.key, LoadClock::now(), 0.0, upload.collision);
    }
    worst.tiles = std::max(worst.tiles, secondsSince(start) * 1000.0);
    if (game.horizon != nullptr) {
        const LoadClock::time_point horizonStart = LoadClock::now();
        game.horizon->update(position, game.frame);
        worst.horizon = std::max(worst.horizon, secondsSince(horizonStart) * 1000.0);
        if (terrain::unreal::horizonAround(game.horizon->clipmap(), place.x, place.z)) {
            timeline.mark(LoadMilestone::Horizon, LoadClock::now());
            if (!game.horizon->busy()) {
                timeline.mark(LoadMilestone::HorizonDetail, LoadClock::now());
            }
        }
    }
    const terrain::ChunkCoord chunk = terrain::unreal::chunkAt(game.frame, position);
    timeline.observe(terrain::unreal::selectedTiles(game.session.streamer(), chunk),
                     LoadClock::now());
    timeline.recordFrame(secondsSince(start) * 1000.0, update.uploads.size());
}

void writeCsv(const Options& options, int index, LoadReason reason, const LoadTimeline& timeline) {
    if (options.csv.empty()) {
        return;
    }
    const std::string path = options.csv + "_" + std::to_string(index) + "_" +
                             (reason == LoadReason::Spawn ? "spawn" : "teleport") + ".csv";
    std::ofstream out(path);
    for (const std::string& row : terrain::unreal::csvRows(timeline.traces())) {
        out << row << '\n';
    }
    std::printf("  CSV: %s\n", path.c_str());
}

// A load at the place: the warm-up of a new pawn at the spawn (as ATerrainWorld::WarmUpAround),
// nothing more at a teleport (as the game today), then frames of 60 Hz until every milestone.
void load(Game& game, const Options& options, int index, LoadReason reason, const Place& place) {
    LoadTimeline& timeline = game.session.timeline();
    const terrain::Heightfield& field = game.session.tiles().heightfield();
    const terrain::unreal::UnrealPoint position = terrain::unreal::toUnreal(
        game.frame, {place.x, static_cast<double>(field.heightAt(place.x, place.z)) + 1.8,
                     place.z});
    const bool detail = game.horizon != nullptr && game.horizon->detail() != nullptr;
    timeline.begin(reason, place.x, place.z, LoadClock::now(),
                   {false, game.horizon != nullptr, detail});
    if (reason == LoadReason::Spawn) {
        const terrain::unreal::WarmUpResult warm = game.session.streamer().warmUp(
            position, game.frame, 1, std::chrono::seconds(10));
        for (const terrain::unreal::StreamerUpdate& update : warm.updates) {
            for (const terrain::unreal::TileUpload& upload : update.uploads) {
                timeline.recordShown(upload.key, LoadClock::now(), 0.0, upload.collision);
            }
        }
    }
    const LoadClock::time_point start = LoadClock::now();
    const auto period = std::chrono::microseconds(16'667);
    LoadClock::time_point next = start;
    Worst worst;
    while (timeline.active() && secondsSince(start) < options.timeoutSeconds) {
        frame(game, place, position, worst);
        next += period;
        std::this_thread::sleep_until(next);
    }
    for (const std::string& line : terrain::unreal::describe(timeline.summary())) {
        std::printf("  %s\n", line.c_str());
    }
    std::printf("  worst update of the tiles %.1f ms, of the horizon %.1f ms\n", worst.tiles,
                worst.horizon);
    writeCsv(options, index, reason, timeline);
}

// The first epoch around the place, level by level: kPiecesTimed pieces of each level computed on
// this thread, the level extrapolated from them.
void levelCosts(const Game& game, const Place& place) {
    constexpr std::size_t kPiecesTimed = 3;
    terrain::HorizonParams params = game.horizon->params();
    params.crestRatio = 0; // deferred: the epochs compute the point samples
    const terrain::HorizonSampler sampler(game.session.tiles().heightfield(),
                                          game.session.tiles().ecology(), params);
    const terrain::HorizonClipmap clipmap(params);
    std::optional<terrain::HorizonEpoch> epoch = clipmap.plan(place.x, place.z);
    std::vector<std::size_t> pieces(static_cast<std::size_t>(params.levels), 0);
    std::vector<double> seconds(pieces.size(), 0.0);
    std::vector<std::size_t> timed(pieces.size(), 0);
    for (terrain::HorizonPiece& piece : epoch->pieces) {
        const auto level = static_cast<std::size_t>(piece.level);
        ++pieces[level];
        if (timed[level] < kPiecesTimed) {
            const LoadClock::time_point start = LoadClock::now();
            terrain::computePiece(sampler, piece);
            seconds[level] += secondsSince(start);
            ++timed[level];
        }
    }
    double total = 0.0;
    for (std::size_t level = 0; level < pieces.size(); ++level) {
        const double estimate = seconds[level] / static_cast<double>(timed[level]) *
                                static_cast<double>(pieces[level]);
        total += estimate;
        std::printf("  level %zu: pitch %d m, %zu pieces, %.1f ms per piece, %.2f s on one "
                    "thread (%.2f s from the finest level on)\n",
                    level, terrain::levelPitch(params, static_cast<int>(level)), pieces[level],
                    seconds[level] * 1000.0 / static_cast<double>(timed[level]), estimate, total);
    }
}

int run(const Options& options) {
    const LoadClock::time_point start = LoadClock::now();
    Game game(sessionParams(options), options);
    std::printf("session: %s, %d workers, %d conversions per frame, ready in %.3f s\n",
                options.planet.empty() ? "no planet file" : options.planet.c_str(),
                options.workers, options.uploads, secondsSince(start));
    if (options.horizon) {
        startHorizon(game, options);
    }
    if (options.levels && game.horizon != nullptr) {
        std::printf("first epoch of the horizon at (%.1f, %.1f) m, level by level:\n",
                    options.spawn.x, options.spawn.z);
        levelCosts(game, options.spawn);
        return EXIT_SUCCESS;
    }
    int index = 0;
    load(game, options, index++, LoadReason::Spawn, options.spawn);
    for (const Place& place : options.teleports) {
        load(game, options, index++, LoadReason::Teleport, place);
    }
    if (options.schedule) {
        std::vector<load_schedule::Place> places{{options.spawn.x, options.spawn.z}};
        for (const Place& place : options.teleports) {
            places.push_back({place.x, place.z});
        }
        for (const std::string& line : load_schedule::report(
                 game.session.tiles(), game.session.streamer().manager().params(), places)) {
            std::printf("%s\n", line.c_str());
        }
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "unreal_loading_bench: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
