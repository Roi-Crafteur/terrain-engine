#pragma once

#include "planet_source.hpp"

#include "terrain/world_size.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace terrain::unreal {

// The planet file of a world switched to another seed while playing (Terrain.Seed, agent 36),
// built aside if need be, never over a file the user gave: the right rivers and lakes for the
// new seed, and the files of the user left as they are.

// Where the planets the tools build for a fixed file of the user live: a file per seed.
inline constexpr const char* kDevPlanetPath = "Saved/TerrainEngine/dev/planet_{Seed}.bin";

// The file to read for seed, and whether it may be built and written there:
// - no file configured (ATerrainWorld::PlanetFile empty): none, the relief alone;
// - a path with "{Seed}": the file of that seed (one per seed, named by the plugin), read, or
//   built and written there as at BeginPlay;
// - a fixed path (a file the user gave): read only if it holds the level of seed, never written;
//   otherwise fallback, the file of seed under kDevPlanetPath (renamed if it is that same file),
//   read or built and written there.
struct PlanetChoice {
    std::string file;
    bool mayWrite = false;
    std::string fallback;

    friend bool operator==(const PlanetChoice&, const PlanetChoice&) = default;
};
[[nodiscard]] PlanetChoice choosePlanetFile(const std::string& configured, std::uint64_t seed,
                                            const std::string& base);

// What a job prepared: the level and how it came (PlanetSource), the file holding it now (the
// path to give the new session), or why there is none.
struct PlanetJobResult {
    PlanetSource source;
    std::string file;
    bool cancelled = false;
    std::string error; // what the build threw, if it did
};

// Prepares the planet file of a choice on a thread of its own (minutes for the whole planet, the
// old world playing meanwhile): reads it when it holds the level of seed, otherwise builds it
// and writes it where the choice allows, as openPlanetSource. Cancel stops a build at the end of
// the stage in progress (seconds); the destructor cancels and waits for that.
class PlanetJob {
public:
    PlanetJob(PlanetChoice choice, std::uint64_t seed, const WorldSize& world);
    ~PlanetJob();
    PlanetJob(const PlanetJob&) = delete;
    PlanetJob& operator=(const PlanetJob&) = delete;

    [[nodiscard]] bool done() const { return done_.load(); }
    // The stage in progress ("load", "field", "relief"... of the build, "save"), for the log.
    [[nodiscard]] std::string stage() const;
    [[nodiscard]] double seconds() const;
    void cancel() { cancel_.store(true); }
    // Waits for the end of the job.
    void wait();
    // What it prepared, once done (wait first otherwise); then empty.
    [[nodiscard]] PlanetJobResult take();

private:
    void run(PlanetChoice choice, std::uint64_t seed, WorldSize world);
    void setStage(const char* stage);

    std::chrono::steady_clock::time_point start_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    mutable std::mutex mutex_;
    std::string stage_;
    double seconds_ = -1.0; // once done
    PlanetJobResult result_;
    std::thread thread_; // last: started once every member above exists
};

} // namespace terrain::unreal
