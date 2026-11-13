#include "edit_save.hpp"

#include "terrain/storage/storage_error.hpp"
#include "terrain/storage/world_save.hpp"

#include <chrono>
#include <exception>
#include <system_error>
#include <utility>

namespace terrain::unreal {

namespace fs = std::filesystem;

namespace {

bool worldThere(const fs::path& folder) {
    std::error_code error;
    const fs::path meta = folder / kWorldMetaFileName;
    fs::path backup = meta;
    backup += ".bak";
    return fs::exists(meta, error) || fs::exists(backup, error);
}

std::string refusal(const fs::path& folder, const std::string& why) {
    return "the saved edits in " + folder.generic_string() + " are not loaded: " + why;
}

} // namespace

fs::path editSaveFolder(const fs::path& root, std::uint64_t seed) {
    return root / ("seed_" + std::to_string(seed));
}

WorldMeta editWorldMeta(std::uint64_t seed, const WorldSize& world) {
    WorldMeta meta;
    meta.name = "Edits of seed " + std::to_string(seed);
    meta.seed = seed;
    meta.sizeX = static_cast<std::uint64_t>(world.x);
    meta.sizeZ = static_cast<std::uint64_t>(world.z);
    meta.createdAt = unixNow();
    meta.savedAt = meta.createdAt;
    return meta;
}

WorldMeta keptWorldMeta(const fs::path& folder, std::uint64_t seed, const WorldSize& world) {
    if (worldThere(folder)) {
        try {
            WorldMeta meta = loadWorldMeta(folder).meta;
            if (meta.seed == seed && static_cast<double>(meta.sizeX) == world.x &&
                static_cast<double>(meta.sizeZ) == world.z) {
                return meta;
            }
        } catch (const std::exception&) {
            // Unreadable: the defaults, as for a folder without a world.
        }
    }
    return editWorldMeta(seed, world);
}

EditLoad loadEdits(const fs::path& folder, std::uint64_t seed, const WorldSize& world) {
    EditLoad load;
    if (!worldThere(folder)) {
        return load;
    }
    load.found = true;
    try {
        LoadedWorld loaded = loadWorld(folder);
        if (loaded.meta.seed != seed) {
            load.error = refusal(folder, "they belong to the seed " +
                                             std::to_string(loaded.meta.seed) + ", not " +
                                             std::to_string(seed));
            return load;
        }
        if (static_cast<double>(loaded.meta.sizeX) != world.x ||
            static_cast<double>(loaded.meta.sizeZ) != world.z) {
            load.error = refusal(folder, "they belong to a world of another size");
            return load;
        }
        load.modifications = std::move(loaded.modifications);
        load.meta = std::move(loaded.meta);
        load.warnings = std::move(loaded.warnings);
    } catch (const std::exception& exception) {
        load.error = refusal(folder, exception.what());
    }
    return load;
}

EditSaver::EditSaver(fs::path folder, WorldMeta meta, Snapshot snapshot)
    : folder_(std::move(folder)), meta_(std::move(meta)), snapshot_(std::move(snapshot)),
      thread_([this] { loop(); }) {}

EditSaver::~EditSaver() {
    flush();
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
}

void EditSaver::markEdited(std::span<const ChunkCoord> chunks) {
    const std::lock_guard lock(mutex_);
    for (const ChunkCoord& chunk : chunks) {
        marked_.insert(regionOf(chunk));
    }
}

void EditSaver::markRegions(std::span<const RegionCoord> regions) {
    const std::lock_guard lock(mutex_);
    marked_.insert(regions.begin(), regions.end());
}

std::size_t EditSaver::dirtyRegions() const {
    const std::lock_guard lock(mutex_);
    return marked_.size();
}

bool EditSaver::save() {
    {
        const std::lock_guard lock(mutex_);
        if (marked_.empty()) {
            return false;
        }
        pending_.merge(marked_);
        marked_.clear();
    }
    wake_.notify_all();
    return true;
}

void EditSaver::flush() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return pending_.empty() && !running_; });
}

EditSaver::Report EditSaver::report() const {
    const std::lock_guard lock(mutex_);
    return report_;
}

void EditSaver::loop() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
        if (pending_.empty()) {
            return; // stopping, nothing left to write
        }
        const std::vector<RegionCoord> regions(pending_.begin(), pending_.end());
        pending_.clear();
        running_ = true;
        WorldMeta meta = meta_;
        lock.unlock();
        const auto start = std::chrono::steady_clock::now();
        std::string error;
        try {
            meta.savedAt = unixNow();
            const ModificationStore edits = snapshot_();
            std::error_code ignored;
            fs::create_directories(folder_, ignored);
            saveWorldRegions(folder_, meta, edits, regions);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - start;
        lock.lock();
        running_ = false;
        report_.lastError = error;
        if (!error.empty()) {
            marked_.insert(regions.begin(), regions.end()); // written again by the next save
        } else {
            ++report_.saves;
            report_.regions = regions.size();
            report_.lastMilliseconds = took.count();
            meta_.savedAt = meta.savedAt;
        }
        idle_.notify_all();
    }
}

} // namespace terrain::unreal
