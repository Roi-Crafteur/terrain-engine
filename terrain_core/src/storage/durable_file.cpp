#include "durable_file.hpp"

#include "file_commit.hpp"
#include "terrain/storage/storage_error.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace terrain::storage {

namespace {

// At most 1 GiB per write call, which every platform accepts.
constexpr std::size_t kMaxWrite = std::size_t{1} << 30;

[[noreturn]] void fail(const char* action, const std::filesystem::path& path, int error) {
    throw StorageError(StorageErrorKind::Io, std::string("cannot ") + action + " " +
                                                 pathText(path) + ": " +
                                                 std::generic_category().message(error));
}

#ifdef _WIN32

// The file descriptors of the C runtime, opened with the wide path: every file name works.
int openForWriting(const std::filesystem::path& file) {
    int descriptor = -1;
    const int flags = _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY;
    const int error =
        _wsopen_s(&descriptor, file.c_str(), flags, _SH_DENYWR, _S_IREAD | _S_IWRITE);
    return error == 0 ? descriptor : -1;
}

long long writeSome(int descriptor, const std::uint8_t* data, std::size_t size) {
    return _write(descriptor, data, static_cast<unsigned int>(std::min(size, kMaxWrite)));
}

int flushToDisk(int descriptor) {
    return _commit(descriptor);
}

int closeFile(int descriptor) {
    return _close(descriptor);
}

#else

int openForWriting(const std::filesystem::path& file) {
    return ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

long long writeSome(int descriptor, const std::uint8_t* data, std::size_t size) {
    return ::write(descriptor, data, std::min(size, kMaxWrite));
}

int flushToDisk(int descriptor) {
    return ::fsync(descriptor);
}

int closeFile(int descriptor) {
    return ::close(descriptor);
}

// A file system that cannot flush a directory (or the drive cache) makes its renames as durable
// as it can: not an error.
bool unsupported(int error) {
    return error == EINVAL || error == ENOTSUP || error == ENOTTY;
}

void syncDirectory(const std::filesystem::path& directory, [[maybe_unused]] bool flushDrive) {
    const int descriptor = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        fail("open", directory, errno);
    }
    int result = ::fsync(descriptor);
    int error = result == 0 ? 0 : errno;
#ifdef __APPLE__
    if (result == 0 && flushDrive && ::fcntl(descriptor, F_FULLFSYNC) != 0) {
        result = -1;
        error = errno;
    }
#endif
    ::close(descriptor);
    if (result != 0 && !unsupported(error)) {
        fail("flush", directory, error);
    }
}

#endif

} // namespace

DurableWriter::DurableWriter(const std::filesystem::path& file) : file_(file) {
    descriptor_ = openForWriting(file);
    if (descriptor_ < 0) {
        fail("create", file, errno);
    }
}

DurableWriter::~DurableWriter() {
    if (descriptor_ >= 0) {
        closeFile(descriptor_);
    }
}

void DurableWriter::write(std::span<const std::uint8_t> bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const long long count =
            writeSome(descriptor_, bytes.data() + written, bytes.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            fail("write", file_, count < 0 ? errno : EIO);
        }
        written += static_cast<std::size_t>(count);
    }
}

void DurableWriter::finish() {
    if (flushToDisk(descriptor_) != 0) {
        fail("flush", file_, errno);
    }
    const int descriptor = descriptor_;
    descriptor_ = -1;
    if (closeFile(descriptor) != 0) {
        fail("close", file_, errno);
    }
}

void writeFileDurably(const std::filesystem::path& file, std::span<const std::uint8_t> bytes) {
    DurableWriter out(file);
    out.write(bytes);
    out.finish();
}

#ifdef _WIN32

void syncDirectories(std::span<const std::filesystem::path>) {}

#else

void syncDirectories(std::span<const std::filesystem::path> directories) {
    for (std::size_t i = 0; i < directories.size(); ++i) {
        syncDirectory(directories[i], i + 1 == directories.size());
    }
}

#endif

} // namespace terrain::storage
