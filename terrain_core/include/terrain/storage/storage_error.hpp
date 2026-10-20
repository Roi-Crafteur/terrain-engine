#pragma once

#include <stdexcept>
#include <string>

namespace terrain {

// Why a world, a region or a modification file could not be read or written.
enum class StorageErrorKind {
    Io,           // the file system refused a read, a write, a rename or a removal
    Truncated,    // the file is shorter than its header announces
    Corrupt,      // the checksum does not match: the bytes were damaged
    NewerVersion, // written by a newer version of the game: never read, never replaced
    Invalid,      // not a file of this kind, or a value it cannot hold
};

// Error of the storage. what() is a clear message for the player or the server log.
class StorageError : public std::runtime_error {
public:
    StorageError(StorageErrorKind kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}

    [[nodiscard]] StorageErrorKind kind() const { return kind_; }

private:
    StorageErrorKind kind_;
};

} // namespace terrain
