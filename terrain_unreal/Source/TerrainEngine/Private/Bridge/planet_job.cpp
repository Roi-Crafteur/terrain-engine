#include "planet_job.hpp"

#include "terrain/storage/planet_file.hpp"
#include "terrain/storage/storage_error.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

namespace terrain::unreal {

namespace {

// Thrown by the probe of a cancelled build at the end of a stage.
struct Cancelled {};

std::filesystem::path pathOf(const std::string& utf8) {
    return {reinterpret_cast<const char8_t*>(utf8.c_str())};
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

PlanetChoice choosePlanetFile(const std::string& configured, std::uint64_t seed,
                              const std::string& base) {
    PlanetChoice choice;
    if (configured.empty()) {
        return choice;
    }
    choice.file = resolvePlanetPath(configured, seed, base);
    if (configured.find("{Seed}") != std::string::npos) {
        choice.mayWrite = true;
        return choice;
    }
    choice.fallback = resolvePlanetPath(kDevPlanetPath, seed, base);
    if (choice.fallback == choice.file) {
        choice.fallback = resolvePlanetPath("Saved/TerrainEngine/dev/planet_{Seed}_built.bin",
                                            seed, base);
    }
    return choice;
}

PlanetJob::PlanetJob(PlanetChoice choice, std::uint64_t seed, const WorldSize& world)
    : start_(std::chrono::steady_clock::now()),
      thread_([this, choice = std::move(choice), seed, world]() mutable {
          run(std::move(choice), seed, world);
      }) {}

PlanetJob::~PlanetJob() {
    cancel();
    wait();
}

void PlanetJob::wait() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::string PlanetJob::stage() const {
    const std::lock_guard lock(mutex_);
    return stage_;
}

double PlanetJob::seconds() const {
    const std::lock_guard lock(mutex_);
    return seconds_ >= 0.0 ? seconds_ : secondsSince(start_);
}

PlanetJobResult PlanetJob::take() {
    wait();
    const std::lock_guard lock(mutex_);
    return std::exchange(result_, PlanetJobResult{});
}

void PlanetJob::setStage(const char* stage) {
    {
        const std::lock_guard lock(mutex_);
        stage_ = stage;
    }
    if (cancel_.load()) {
        throw Cancelled{};
    }
}

void PlanetJob::run(PlanetChoice choice, std::uint64_t seed, WorldSize world) {
    PlanetJobResult result;
    try {
        setStage("load");
        std::string file = choice.file;
        if (!file.empty() && !choice.mayWrite) {
            // A file of the user: read when it holds this seed, never written.
            try {
                result.source.level = std::make_shared<const PlanetLevel>(
                    loadPlanetFile(pathOf(file), seed, PlanetParams{}, world));
                result.source.seconds = secondsSince(start_);
            } catch (const StorageError& e) {
                result.source.reason = e.what(); // it names the file
                file = choice.fallback;
            }
        }
        if (!file.empty() && result.source.level == nullptr) {
            const std::string why = result.source.reason;
            result.source = openPlanetSource(pathOf(file), seed, world,
                                             [this](const char* stage) { setStage(stage); });
            if (!why.empty() && result.source.built) {
                result.source.reason = why + "; " + result.source.reason;
            }
        }
        result.file = file;
    } catch (const Cancelled&) {
        result = PlanetJobResult{};
        result.cancelled = true;
    } catch (const std::exception& e) {
        result = PlanetJobResult{};
        result.error = e.what();
    }
    const std::lock_guard lock(mutex_);
    result_ = std::move(result);
    seconds_ = secondsSince(start_);
    stage_ = "done";
    done_.store(true);
}

} // namespace terrain::unreal
