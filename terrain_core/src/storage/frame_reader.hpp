#pragma once

// A framed file (storage_codec.hpp) read in pieces, its checksum taken as it goes: the body read
// as by a ByteReader without the whole file in memory (the planet file: hundreds of megabytes).
// Shared by the files of src/storage only.

#include "storage_codec.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace terrain::storage {

// The body is read kFramePiece bytes at a time.
inline constexpr std::size_t kFramePiece = std::size_t{1} << 20;

class FrameReader {
public:
    // Opens file, what naming it in the messages, and checks its frame as openFrame does: throws
    // the same errors (Io if it cannot be read), but a wrong checksum of a frame otherwise valid,
    // found once the body is read (finish), or when a read fails before (fail). Reads piece
    // bytes at a time (at least 1).
    FrameReader(const std::filesystem::path& file, const Magic& magic, std::uint32_t current,
                const std::string& what, std::size_t piece = kFramePiece);

    [[nodiscard]] std::uint32_t version() const { return version_; }

    std::uint8_t u8() { return static_cast<std::uint8_t>(little(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(little(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(little(4)); }
    std::uint64_t u64() { return little(8); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    float f32() { return finite(std::bit_cast<float>(u32())); }
    double f64() { return finite(std::bit_cast<double>(u64())); }
    // A count of records of recordSize bytes (at least 1), which must fit in the bytes left.
    std::size_t count(std::size_t recordSize) {
        const std::size_t value = u32();
        if (value > remaining() / recordSize) {
            fail("truncated");
        }
        return value;
    }
    // The next size bytes, until the next read.
    std::span<const std::uint8_t> take(std::size_t size);
    // The bytes of the body left to read.
    [[nodiscard]] std::size_t remaining() const {
        return static_cast<std::size_t>(bodyEnd_ - (offset_ + position_));
    }
    // Fails with what unless every byte of the body was read.
    void expectEnd(const char* what) {
        if (remaining() != 0) {
            fail(what);
        }
    }
    // Throws a StorageError (Invalid) whose message is what, after the file is named; Corrupt
    // instead when the checksum of the file is wrong (its damage comes first, as in openFrame).
    [[noreturn]] void fail(const std::string& what);
    // Reads what is left of the file and checks its checksum: throws Corrupt if it is wrong.
    void finish();

private:
    std::uint64_t little(int size) {
        std::uint64_t value = 0;
        for (int i = 0; i < size; ++i) {
            if (position_ == filled_) {
                refill();
            }
            value |= static_cast<std::uint64_t>(piece_[position_++]) << (8 * i);
        }
        return value;
    }
    template <class T>
    T finite(T value) {
        if (!std::isfinite(value)) {
            fail("non-finite value");
        }
        return value;
    }
    // The next piece of the body (failing truncated at its end), its bytes added to the checksum.
    void refill();
    // Whether the checksum of the file holds, what is left of it read.
    bool checksumHolds();
    void read(std::uint8_t* to, std::size_t size);

    std::ifstream in_;
    std::string path_; // in the messages of Io
    std::string what_;
    std::uint32_t version_ = 0;
    std::uint64_t bodyEnd_ = 0; // the offset of the checksum in the file
    std::uint64_t offset_ = 0;  // the offset of the piece in the file
    std::vector<std::uint8_t> piece_;
    std::size_t position_ = 0;
    std::size_t filled_ = 0;
    std::vector<std::uint8_t> taken_;
    std::uint32_t hash_ = 0;
    bool done_ = false; // the whole file read: holds_ tells whether its checksum holds
    bool holds_ = false;
};

} // namespace terrain::storage
