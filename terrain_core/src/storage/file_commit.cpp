#include "file_commit.hpp"

#include "durable_file.hpp"
#include "terrain/storage/storage_error.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <set>
#include <system_error>
#include <thread>

namespace terrain::storage {

namespace {

namespace fs = std::filesystem;

// On Windows a file another program holds open (a reader, an antivirus, an indexer, a backup)
// can be neither renamed nor replaced: a rename is tried again after these pauses, about 1.3 s
// in all, before the save fails.
constexpr std::array<int, 7> kRenamePausesMs = {10, 20, 40, 80, 160, 320, 640};

[[noreturn]] void fail(const std::string& action, const fs::path& path,
                       const std::error_code& error) {
    throw StorageError(StorageErrorKind::Io,
                       "cannot " + action + " " + pathText(path) + ": " + error.message());
}

bool isRegularFile(const fs::path& file) {
    std::error_code ignored;
    return fs::is_regular_file(file, ignored);
}

// The directory of a file, "." for a bare file name.
fs::path directoryOf(const fs::path& file) {
    const fs::path parent = file.parent_path();
    return parent.empty() ? fs::path(".") : parent;
}

void renameRetrying(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::rename(from, to, error);
    for (const int pause : kRenamePausesMs) {
        if (!error) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pause));
        fs::rename(from, to, error);
    }
    if (error) {
        throw StorageError(StorageErrorKind::Io,
                           "cannot replace " + pathText(to) + " by " + pathText(from) +
                               " (is it open in another program?): " + error.message());
    }
}

void removeFile(const fs::path& file) {
    std::error_code error;
    fs::remove(file, error);
    if (error) {
        fail("remove", file, error);
    }
}

// Creates directory and its missing parents; the directories holding a new one are touched.
void createMissing(const fs::path& directory, std::set<fs::path>& touched) {
    std::error_code error;
    if (directory.empty() || fs::is_directory(directory, error)) {
        return;
    }
    createMissing(directory.parent_path(), touched);
    static_cast<void>(fs::create_directory(directory, error));
    if (error) {
        fail("create the directory", directory, error);
    }
    touched.insert(directoryOf(directory));
}

// Removes the temporary files of writes, as far as possible: after a failure.
void removeTemporaries(std::span<const FileWrite> writes) {
    for (const FileWrite& write : writes) {
        const fs::path temporary = withSuffix(write.file, kTemporarySuffix);
        if (isRegularFile(temporary)) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        }
    }
}

void replaceFiles(std::span<const FileWrite> writes, std::span<const fs::path> removals,
                  bool keepBackups, std::set<fs::path>& touched) {
    for (const fs::path& file : removals) {
        removeFile(withSuffix(file, kBackupSuffix));
        removeFile(file);
        touched.insert(directoryOf(file));
    }
    for (std::size_t i = 0; i < writes.size(); ++i) {
        const fs::path& file = writes[i].file;
        try {
            if (keepBackups && isRegularFile(file)) {
                renameRetrying(file, withSuffix(file, kBackupSuffix));
            }
            renameRetrying(withSuffix(file, kTemporarySuffix), file);
        } catch (const StorageError&) {
            removeTemporaries(writes.subspan(i));
            throw;
        }
    }
}

// Reads file and decodes it; the errors of decode gain the path of the file.
void decodeFile(const fs::path& file,
                const std::function<void(std::span<const std::uint8_t>)>& decode) {
    const std::vector<std::uint8_t> bytes = readFileBytes(file);
    try {
        decode(bytes);
    } catch (const StorageError& error) {
        throw StorageError(error.kind(), pathText(file) + ": " + error.what());
    }
}

bool isDamage(StorageErrorKind kind) {
    return kind == StorageErrorKind::Truncated || kind == StorageErrorKind::Corrupt;
}

} // namespace

std::string pathText(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path withSuffix(const fs::path& file, const char* suffix) {
    fs::path result = file;
    result += suffix;
    return result;
}

bool hasFileOrBackup(const fs::path& file) {
    return isRegularFile(file) || isRegularFile(withSuffix(file, kBackupSuffix));
}

std::vector<std::uint8_t> readFileBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        throw StorageError(StorageErrorKind::Io, "cannot open " + pathText(file));
    }
    const std::streamoff size = in.tellg();
    if (size < 0) {
        throw StorageError(StorageErrorKind::Io, "cannot read " + pathText(file));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    }
    if (!in) {
        throw StorageError(StorageErrorKind::Io, "cannot read " + pathText(file));
    }
    return bytes;
}

void commitFiles(std::span<const FileWrite> writes, std::span<const fs::path> removals,
                 const CommitOptions& options) {
    std::set<fs::path> touched;
    try {
        for (const FileWrite& write : writes) {
            const fs::path directory = directoryOf(write.file);
            if (options.createDirectories) {
                createMissing(directory, touched);
            }
            writeFileDurably(withSuffix(write.file, kTemporarySuffix), write.bytes);
            touched.insert(directory);
        }
    } catch (const StorageError&) {
        removeTemporaries(writes);
        throw;
    }
    replaceFiles(writes, removals, options.keepBackups, touched);
    const std::vector<fs::path> directories(touched.begin(), touched.end());
    syncDirectories(directories);
}

bool decodeWithBackup(const fs::path& file,
                      const std::function<void(std::span<const std::uint8_t>)>& decode) {
    const fs::path backup = withSuffix(file, kBackupSuffix);
    if (!isRegularFile(file)) {
        if (!isRegularFile(backup)) {
            throw StorageError(StorageErrorKind::Io, "missing " + pathText(file));
        }
        decodeFile(backup, decode);
        return true;
    }
    try {
        decodeFile(file, decode);
        return false;
    } catch (const StorageError& damaged) {
        if (!isDamage(damaged.kind()) || !isRegularFile(backup)) {
            throw;
        }
        try {
            decodeFile(backup, decode);
        } catch (const StorageError& backupError) {
            throw StorageError(damaged.kind(),
                               std::string(damaged.what()) + "; its backup: " + backupError.what());
        }
        return true;
    }
}

} // namespace terrain::storage
