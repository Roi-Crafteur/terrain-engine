#pragma once

#include "terrain/storage/world_meta.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terrain {

// The worlds of a directory, one folder each (world_save.hpp). The name of the folder is the
// stable identifier of a world; the name shown to the player is in its world.meta.

struct WorldEntry {
    std::string folder;            // the folder of the world in the directory
    std::optional<WorldMeta> meta; // empty if world.meta cannot be read
    std::string error;             // why, when meta is empty
    bool restored = false;         // world.meta damaged or missing, read from world.meta.bak
};

// The worlds of root, sorted by folder: the folders holding a world.meta or a world.meta.bak.
// A damaged world is listed with its error rather than hidden. No worlds if root does not
// exist. Throws StorageError (Io) if root cannot be read.
[[nodiscard]] std::vector<WorldEntry> listWorlds(const std::filesystem::path& root);

// Base folder name of a world named name: lowercase ASCII letters and digits and '-', any other
// character replaced by '_' (runs of them collapsed, none at the ends), at most 32 characters,
// "world" if nothing is left, and '_' appended to a name Windows reserves ("con", "aux",
// "com1"...).
[[nodiscard]] std::string worldFolderName(std::string_view name);

// Creates a world in root (created if needed): a new folder, worldFolderName of meta.name or,
// if taken, followed by "-2", "-3"...; then its world.meta, written last, so that a folder
// without it is never a world. Returns the folder. Throws StorageError: Invalid for an invalid
// meta, before anything is created.
[[nodiscard]] std::string createWorld(const std::filesystem::path& root, const WorldMeta& meta);

// Changes the name shown of a world; its folder stays. Throws StorageError.
void renameWorld(const std::filesystem::path& root, std::string_view folder,
                 std::string_view name);

// Deletes a world: its world.meta.bak, then its world.meta (the world disappears at once), then
// its folder. Throws StorageError (Invalid), changing nothing, unless folder is a plain name
// (no separator, not "." or "..") of a directory of root, not a symbolic link, holding a
// world.meta or a world.meta.bak that starts with the magic of the world format.
void deleteWorld(const std::filesystem::path& root, std::string_view folder);

// Converts a modification file of the previous format (TMOD v1) into a new world of root, as
// createWorld does: its seed and its modifications, the other fields from meta, the generator
// kLegacyGeneratorVersion. The TMOD file is left as it was. Returns the folder of the world.
// Throws StorageError, and leaves no world behind, if the file or the meta is invalid.
[[nodiscard]] std::string migrateModificationFile(const std::filesystem::path& file,
                                                  const std::filesystem::path& root,
                                                  WorldMeta meta);

} // namespace terrain
