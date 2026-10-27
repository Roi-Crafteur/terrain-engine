// A framed file read in pieces (frame_reader.hpp).
#include "frame_reader.hpp"

#include "file_commit.hpp"

#include <algorithm>
#include <array>

namespace terrain::storage {

namespace {

// The FNV-1a of modifications.hpp, piece by piece: hash the value over the bytes before.
constexpr std::uint32_t kFnvStart = 2166136261U;
std::uint32_t fnv1aOn(std::uint32_t hash, std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 16777619U;
    }
    return hash;
}

} // namespace

FrameReader::FrameReader(const std::filesystem::path& file, const Magic& magic,
                         std::uint32_t current, const std::string& what, std::size_t piece)
    : in_(file, std::ios::binary | std::ios::ate), path_(pathText(file)), what_(what),
      piece_(std::max<std::size_t>(1, piece)) {
    if (!in_) {
        throw StorageError(StorageErrorKind::Io, "cannot open " + path_);
    }
    const std::streamoff size = in_.tellg();
    if (size < 0) {
        throw StorageError(StorageErrorKind::Io, "cannot read " + path_);
    }
    in_.seekg(0);
    const auto fileSize = static_cast<std::uint64_t>(size);
    if (fileSize < kFrameSize) {
        throw StorageError(StorageErrorKind::Truncated,
                           what_ + ": truncated (" + std::to_string(fileSize) + " bytes)");
    }
    std::array<std::uint8_t, kFrameHeaderSize> header{};
    read(header.data(), header.size());
    hash_ = fnv1aOn(kFnvStart, header);
    bodyEnd_ = fileSize - 4;
    offset_ = kFrameHeaderSize;
    const FrameHeader h = frameHeaderOf(header, magic, what_);
    // A frame that only its checksum may fail is read on, its checksum checked at the end.
    const bool later = h.ours && h.version != 0 && h.version <= current;
    checkFrame(h, fileSize, current, what_, [&] { return later || checksumHolds(); });
    version_ = h.version;
}

std::span<const std::uint8_t> FrameReader::take(std::size_t size) {
    if (size > remaining()) {
        fail("truncated");
    }
    taken_.resize(size);
    for (std::size_t done = 0; done < size;) {
        if (position_ == filled_) {
            refill();
        }
        const std::size_t part = std::min(size - done, filled_ - position_);
        std::copy_n(piece_.begin() + static_cast<std::ptrdiff_t>(position_), part,
                    taken_.begin() + static_cast<std::ptrdiff_t>(done));
        position_ += part;
        done += part;
    }
    return taken_;
}

void FrameReader::fail(const std::string& what) {
    if (!checksumHolds()) {
        throw StorageError(StorageErrorKind::Corrupt,
                           what_ + ": checksum mismatch: the file is damaged");
    }
    throw StorageError(StorageErrorKind::Invalid, what_ + ": " + what);
}

void FrameReader::finish() {
    if (!checksumHolds()) {
        throw StorageError(StorageErrorKind::Corrupt,
                           what_ + ": checksum mismatch: the file is damaged");
    }
}

void FrameReader::refill() {
    offset_ += filled_;
    position_ = 0;
    filled_ = 0;
    const auto size =
        static_cast<std::size_t>(std::min<std::uint64_t>(piece_.size(), bodyEnd_ - offset_));
    if (size == 0) {
        fail("truncated");
    }
    read(piece_.data(), size);
    hash_ = fnv1aOn(hash_, std::span<const std::uint8_t>(piece_).first(size));
    filled_ = size;
}

bool FrameReader::checksumHolds() {
    if (done_) {
        return holds_;
    }
    done_ = true;
    while (offset_ + filled_ < bodyEnd_) {
        offset_ += filled_;
        filled_ =
            static_cast<std::size_t>(std::min<std::uint64_t>(piece_.size(), bodyEnd_ - offset_));
        read(piece_.data(), filled_);
        hash_ = fnv1aOn(hash_, std::span<const std::uint8_t>(piece_).first(filled_));
    }
    position_ = filled_;
    std::array<std::uint8_t, 4> trailer{};
    read(trailer.data(), trailer.size());
    const std::uint32_t stored = static_cast<std::uint32_t>(trailer[0]) |
                                 static_cast<std::uint32_t>(trailer[1]) << 8 |
                                 static_cast<std::uint32_t>(trailer[2]) << 16 |
                                 static_cast<std::uint32_t>(trailer[3]) << 24;
    holds_ = stored == hash_;
    return holds_;
}

void FrameReader::read(std::uint8_t* to, std::size_t size) {
    in_.read(reinterpret_cast<char*>(to), static_cast<std::streamsize>(size));
    if (!in_) {
        throw StorageError(StorageErrorKind::Io, "cannot read " + path_);
    }
}

} // namespace terrain::storage
