#pragma once

// Reading and replacing the storage files without ever leaving a damaged one. Shared by the
// files of src/storage only.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace terrain::storage {

// Never read: the new content of a file, before it replaces the file.
inline constexpr const char* kTemporarySuffix = ".tmp";
// The previous version of a file, read when the file is damaged or missing.
inline constexpr const char* kBackupSuffix = ".bak";

// Text of a path for a message, in UTF-8: path::string() throws on Windows for a name outside
// the ANSI code page.
[[nodiscard]] std::string pathText(const std::filesystem::path& path);

// file followed by suffix, such as world.meta.bak.
[[nodiscard]] std::filesystem::path withSuffix(const std::filesystem::path& file,
                                               const char* suffix);

// file or its .bak copy exists.
[[nodiscard]] bool hasFileOrBackup(const std::filesystem::path& file);

// Whole content of a file. Throws StorageError (Io) if it cannot be read.
[[nodiscard]] std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& file);

struct FileWrite {
    std::filesystem::path file;
    std::vector<std::uint8_t> bytes;
};

struct CommitOptions {
    bool keepBackups = false;       // the replaced version of each file becomes <file>.bak
    bool createDirectories = false; // creates the missing directories of the written files
};

// Writes and removes files so that neither a crash nor a power cut leaves a damaged one:
// 1. each new content is written to <file>.tmp and flushed to the disk;
// 2. each removed file loses <file>.bak, then <file>; then, in order, each written <file>.tmp
//    replaces <file> (which first becomes <file>.bak with keepBackups);
// 3. each directory touched is flushed, once (syncDirectories).
// A failure in step 1 changes no file and removes the temporary files; in step 2, the files
// already replaced stay replaced, each one whole. Throws StorageError (Io).
void commitFiles(std::span<const FileWrite> writes,
                 std::span<const std::filesystem::path> removals, const CommitOptions& options);

class DurableWriter;

// commitFiles of one file whose new content write writes in pieces into <file>.tmp (a content
// too large to hold in memory): the same steps, the same guarantees. Whatever write throws
// leaves file as it was, removes <file>.tmp and is thrown again.
void commitStreamedFile(const std::filesystem::path& file,
                        const std::function<void(DurableWriter&)>& write,
                        const CommitOptions& options);

// Decodes the bytes of file with decode, which throws a StorageError if they are invalid. If
// file is damaged (Truncated or Corrupt) or missing, decodes <file>.bak instead and returns true.
// Throws the StorageError of file, with its path in the message (and the error of the backup if
// it is damaged too), or Io if neither exists.
bool decodeWithBackup(const std::filesystem::path& file,
                      const std::function<void(std::span<const std::uint8_t>)>& decode);

} // namespace terrain::storage
