#include "terrain/storage/world_directory.hpp"

#include "durable_file.hpp"
#include "file_commit.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/storage/storage_error.hpp"
#include "terrain/storage/world_save.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <utility>

namespace terrain {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxFolderBytes = 32;
constexpr int kMaxFolderSuffix = 10'000;
// The magic of world.meta (world_meta.cpp).
constexpr std::array<char, 4> kWorldMagic = {'T', 'W', 'L', 'D'};

[[noreturn]] void ioError(const std::string& action, const fs::path& path,
                          const std::error_code& error) {
    throw StorageError(StorageErrorKind::Io, "cannot " + action + " " + storage::pathText(path) +
                                                 ": " + error.message());
}

[[noreturn]] void refuse(const std::string& action, std::string_view folder,
                         const std::string& why) {
    throw StorageError(StorageErrorKind::Invalid,
                       "cannot " + action + " \"" + std::string(folder) + "\": " + why);
}

bool reservedOnWindows(const std::string& name) {
    constexpr std::array<std::string_view, 4> kDevices = {"con", "prn", "aux", "nul"};
    if (std::find(kDevices.begin(), kDevices.end(), name) != kDevices.end()) {
        return true;
    }
    return name.size() == 4 && (name.starts_with("com") || name.starts_with("lpt")) &&
           name[3] >= '1' && name[3] <= '9';
}

// The folder of a world in root, from its UTF-8 name (as listWorlds gives it), which must be a
// plain name.
fs::path worldFolder(const fs::path& root, std::string_view folder, const std::string& action) {
    const bool plain = !folder.empty() && folder != "." && folder != ".." &&
                       folder.find_first_of(std::string_view("/\\:\0", 4)) == std::string::npos;
    if (!plain) {
        refuse(action, folder, "not the name of a world folder");
    }
    std::u8string name;
    for (const char c : folder) {
        name.push_back(static_cast<char8_t>(c));
    }
    return root / fs::path(name);
}

// Creates a free folder for base in root (created if needed): base, base-2, base-3...
std::string reserveFolder(const fs::path& root, const std::string& base) {
    std::error_code error;
    fs::create_directories(root, error);
    if (error) {
        ioError("create the directory", root, error);
    }
    for (int n = 1; n <= kMaxFolderSuffix; ++n) {
        const std::string folder = n == 1 ? base : base + "-" + std::to_string(n);
        if (fs::create_directory(root / folder, error)) {
            return folder;
        }
        std::error_code ignored;
        if (error && !fs::exists(root / folder, ignored)) {
            ioError("create the directory", root / folder, error);
        }
    }
    throw StorageError(StorageErrorKind::Io,
                       "no free folder for " + base + " in " + storage::pathText(root));
}

// A new world in root holding meta and modifications; no folder is left behind on failure.
std::string createFolder(const fs::path& root, const WorldMeta& meta,
                         const ModificationStore& modifications) {
    validateWorldMeta(meta);
    const std::string folder = reserveFolder(root, worldFolderName(meta.name));
    try {
        saveWorld(root / folder, meta, modifications);
    } catch (const StorageError&) {
        std::error_code ignored;
        fs::remove_all(root / folder, ignored);
        throw;
    }
    const std::array<fs::path, 1> directories = {root};
    storage::syncDirectories(directories);
    return folder;
}

bool startsWithWorldMagic(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::array<char, 4> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    return in && magic == kWorldMagic;
}

void removeFile(const fs::path& file) {
    std::error_code error;
    fs::remove(file, error);
    if (error) {
        ioError("remove", file, error);
    }
}

} // namespace

std::vector<WorldEntry> listWorlds(const fs::path& root) {
    std::vector<WorldEntry> worlds;
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        return worlds;
    }
    for (fs::directory_iterator it(root, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code ignored;
        if (!it->is_directory(ignored) ||
            !storage::hasFileOrBackup(it->path() / kWorldMetaFileName)) {
            continue;
        }
        WorldEntry entry;
        entry.folder = storage::pathText(it->path().filename());
        try {
            LoadedMeta loaded = loadWorldMeta(it->path());
            entry.meta = std::move(loaded.meta);
            entry.restored = loaded.restored;
        } catch (const StorageError& failure) {
            entry.error = failure.what();
        }
        worlds.push_back(std::move(entry));
    }
    if (error) {
        ioError("read the directory", root, error);
    }
    std::sort(worlds.begin(), worlds.end(),
              [](const WorldEntry& a, const WorldEntry& b) { return a.folder < b.folder; });
    return worlds;
}

std::string worldFolderName(std::string_view name) {
    std::string folder;
    for (const char c : name) {
        const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '-') {
            folder += lower;
        } else if (!folder.empty() && folder.back() != '_') {
            folder += '_';
        }
    }
    if (folder.size() > kMaxFolderBytes) {
        folder.resize(kMaxFolderBytes);
    }
    while (!folder.empty() && folder.back() == '_') {
        folder.pop_back();
    }
    if (folder.empty()) {
        folder = "world";
    }
    if (reservedOnWindows(folder)) {
        folder += '_';
    }
    return folder;
}

std::string createWorld(const fs::path& root, const WorldMeta& meta) {
    return createFolder(root, meta, {});
}

void renameWorld(const fs::path& root, std::string_view folder, std::string_view name) {
    const fs::path world = worldFolder(root, folder, "rename");
    if (!isValidWorldName(name)) {
        refuse("rename", folder,
               "invalid name, 1 to 128 bytes of UTF-8 without control characters");
    }
    WorldMeta meta = loadWorldMeta(world).meta;
    meta.name = std::string(name);
    saveWorldMeta(world, meta);
}

void deleteWorld(const fs::path& root, std::string_view folder) {
    const fs::path world = worldFolder(root, folder, "delete");
    std::error_code error;
    if (!fs::is_directory(fs::symlink_status(world, error))) {
        refuse("delete", folder, "not a directory (or a symbolic link)");
    }
    const fs::path meta = world / kWorldMetaFileName;
    const fs::path backup = storage::withSuffix(meta, storage::kBackupSuffix);
    if (!startsWithWorldMagic(meta) && !startsWithWorldMagic(backup)) {
        refuse("delete", folder, "not a world, it holds no world.meta");
    }
    removeFile(backup);
    removeFile(meta);
    fs::remove_all(world, error);
    if (error) {
        ioError("delete", world, error);
    }
}

std::string migrateModificationFile(const fs::path& file, const fs::path& root, WorldMeta meta) {
    const SavedModifications saved = loadModifications(file);
    meta.seed = saved.seed;
    meta.generatorVersion = kLegacyGeneratorVersion;
    return createFolder(root, meta, saved.store);
}

} // namespace terrain
