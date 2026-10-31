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

// writeFileDurably in pieces, for a content too large to hold in memory: the file is created or
// truncated by the constructor, written by write, flushed to the disk and closed by finish.
// Destroyed without finish (after a failure), it is closed as it is. Throws StorageError (Io).
class DurableWriter {
public:
    explicit DurableWriter(const std::filesystem::path& file);
    ~DurableWriter();
    DurableWriter(const DurableWriter&) = delete;
    DurableWriter& operator=(const DurableWriter&) = delete;

    void write(std::span<const std::uint8_t> bytes);
    void finish();

private:
    std::filesystem::path file_;
    int descriptor_ = -1;
};

// Makes durable the files created, renamed or removed in these directories: fsync of each one,
// then on macOS a single F_FULLFSYNC, which also flushes the drive cache (slow, hence once for
// all the directories of a save). Nothing on Windows, where NTFS journals its directories.
// Throws StorageError (Io).
void syncDirectories(std::span<const std::filesystem::path> directories);

} // namespace terrain::storage
