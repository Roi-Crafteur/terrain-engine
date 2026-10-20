#include "terrain/storage/world_save.hpp"

#include "file_commit.hpp"
#include "terrain/storage/storage_error.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace terrain {

namespace {

namespace fs = std::filesystem;

struct RegionScan {
    std::set<RegionCoord> regions;     // with a file or a .bak copy
    std::vector<fs::path> temporaries; // abandoned <region file>.tmp
};

[[noreturn]] void cannotRead(const fs::path& directory, const std::error_code& error) {
    throw StorageError(StorageErrorKind::Io, "cannot read the directory " +
                                                 storage::pathText(directory) + ": " +
                                                 error.message());
}

bool removeSuffix(std::string_view& name, std::string_view suffix) {
    if (!name.ends_with(suffix)) {
        return false;
    }
    name.remove_suffix(suffix.size());
    return true;
}

void scanGroup(const fs::path& directory, RegionCoord group, RegionScan& scan) {
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code ignored;
        if (!it->is_regular_file(ignored)) {
            continue;
        }
        const std::string name = storage::pathText(it->path().filename());
        std::string_view base = name;
        const bool temporary = removeSuffix(base, storage::kTemporarySuffix);
        removeSuffix(base, storage::kBackupSuffix);
        const std::optional<RegionCoord> region = parseRegionFileName(base);
        if (!region) {
            continue;
        }
        if (temporary) {
            scan.temporaries.push_back(it->path());
        } else if (regionGroupOf(*region) != group) {
            throw StorageError(StorageErrorKind::Invalid,
                               "misplaced region file " + storage::pathText(it->path()));
        } else {
            scan.regions.insert(*region);
        }
    }
    if (error) {
        cannotRead(directory, error);
    }
}

RegionScan scanRegions(const fs::path& world) {
    RegionScan scan;
    const fs::path directory = world / kRegionDirectoryName;
    std::error_code error;
    if (!fs::is_directory(directory, error)) {
        return scan;
    }
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code ignored;
        const std::optional<RegionCoord> group =
            parseRegionGroupName(storage::pathText(it->path().filename()));
        if (group && it->is_directory(ignored)) {
            scanGroup(it->path(), *group, scan);
        }
    }
    if (error) {
        cannotRead(directory, error);
    }
    return scan;
}

// Commits the regions of writes, then world.meta, each keeping its previous version.
void commit(const fs::path& world, std::vector<std::uint8_t> metaBytes,
            std::vector<storage::FileWrite> writes, std::span<const fs::path> removals) {
    writes.push_back({world / kWorldMetaFileName, std::move(metaBytes)});
    storage::CommitOptions options;
    options.keepBackups = true;
    options.createDirectories = true;
    storage::commitFiles(writes, removals, options);
}

void removeTemporaries(std::span<const fs::path> temporaries) {
    for (const fs::path& temporary : temporaries) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
    }
}

void restore(LoadedWorld& loaded, const fs::path& file) {
    loaded.restored.push_back(file);
    loaded.warnings.push_back(storage::pathText(file) +
                              " was damaged or missing: its previous version was loaded, the "
                              "changes of its last save are lost");
}

} // namespace

void saveWorld(const fs::path& world, const WorldMeta& meta,
               const ModificationStore& modifications) {
    std::vector<std::uint8_t> metaBytes = serializeWorldMeta(meta);
    const RegionScan scan = scanRegions(world);
    const std::vector<RegionCoord> regions = regionsOf(modifications);
    std::vector<storage::FileWrite> writes;
    writes.reserve(regions.size() + 1);
    for (const RegionCoord region : regions) {
        writes.push_back({world / regionRelativePath(region),
                          serializeRegion(modifications, region, meta.seed)});
    }
    std::vector<fs::path> removals;
    for (const RegionCoord region : scan.regions) {
        if (!std::binary_search(regions.begin(), regions.end(), region)) {
            removals.push_back(world / regionRelativePath(region));
        }
    }
    commit(world, std::move(metaBytes), std::move(writes), removals);
    removeTemporaries(scan.temporaries);
}

void saveWorldRegions(const fs::path& world, const WorldMeta& meta,
                      const ModificationStore& modifications,
                      std::span<const RegionCoord> regions) {
    std::vector<std::uint8_t> metaBytes = serializeWorldMeta(meta);
    const std::set<RegionCoord> unique(regions.begin(), regions.end());
    std::vector<storage::FileWrite> writes;
    std::vector<fs::path> removals;
    for (const RegionCoord region : unique) {
        const fs::path file = world / regionRelativePath(region);
        std::vector<std::uint8_t> bytes = serializeRegion(modifications, region, meta.seed);
        if (bytes.empty()) {
            removals.push_back(file);
        } else {
            writes.push_back({file, std::move(bytes)});
        }
    }
    commit(world, std::move(metaBytes), std::move(writes), removals);
}

void saveWorldMeta(const fs::path& world, const WorldMeta& meta) {
    commit(world, serializeWorldMeta(meta), {}, {});
}

LoadedMeta loadWorldMeta(const fs::path& world) {
    LoadedMeta loaded;
    loaded.restored = storage::decodeWithBackup(
        world / kWorldMetaFileName, [&loaded](std::span<const std::uint8_t> bytes) {
            loaded.meta = deserializeWorldMeta(bytes);
        });
    return loaded;
}

LoadedWorld loadWorld(const fs::path& world) {
    LoadedWorld loaded;
    LoadedMeta meta = loadWorldMeta(world);
    loaded.meta = std::move(meta.meta);
    if (meta.restored) {
        restore(loaded, world / kWorldMetaFileName);
    }
    for (const RegionCoord region : savedRegions(world)) {
        LoadedRegion part = loadRegion(world, region, loaded.meta.seed);
        if (part.restored) {
            restore(loaded, world / regionRelativePath(region));
        }
        loaded.modifications.merge(std::move(part.modifications));
    }
    loaded.generatorChanged = generatorChanged(loaded.meta);
    if (loaded.generatorChanged) {
        loaded.warnings.push_back(generatorWarning(loaded.meta));
    }
    return loaded;
}

LoadedRegion loadRegion(const fs::path& world, RegionCoord region, std::uint64_t seed) {
    const fs::path file = world / regionRelativePath(region);
    LoadedRegion loaded;
    if (!storage::hasFileOrBackup(file)) {
        return loaded;
    }
    loaded.restored = storage::decodeWithBackup(
        file, [&loaded, region, seed](std::span<const std::uint8_t> bytes) {
            loaded.modifications = deserializeRegion(bytes, region, seed);
        });
    return loaded;
}

std::vector<RegionCoord> savedRegions(const fs::path& world) {
    const RegionScan scan = scanRegions(world);
    return {scan.regions.begin(), scan.regions.end()};
}

} // namespace terrain
