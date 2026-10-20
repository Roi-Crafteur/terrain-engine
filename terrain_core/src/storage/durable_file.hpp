#pragma once

// Flushing files and directories to the disk: the only platform-specific code of terrain_core,
// all of it in durable_file.cpp. Shared by the files of src/storage only.

#include <cstdint>
#include <filesystem>
#include <span>

namespace terrain::storage {

// Creates or truncates file, writes bytes and flushes them to the disk before returning: fsync
// on Linux and macOS (on macOS, up to the drive, whose cache syncDirectories flushes), _commit
// on Windows. Throws StorageError (Io).
void writeFileDurably(const std::filesystem::path& file, std::span<const std::uint8_t> bytes);

// Makes durable the files created, renamed or removed in these directories: fsync of each one,
// then on macOS a single F_FULLFSYNC, which also flushes the drive cache (slow, hence once for
// all the directories of a save). Nothing on Windows, where NTFS journals its directories.
// Throws StorageError (Io).
void syncDirectories(std::span<const std::filesystem::path> directories);

} // namespace terrain::storage
