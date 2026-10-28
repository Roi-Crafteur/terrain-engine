#pragma once

// A frame (storage_codec.hpp) written in pieces, for a file too large to hold in memory (the
// planet file). The writers below take the values of a ByteWriter: ByteCounter only counts their
// bytes (the size the header of the frame announces), FrameStream writes the frame through a
// DurableWriter, its FNV-1a taken as it goes: the bytes of beginFrame, the body, sealFrame.
// Shared by the files of src/storage only.

#include "durable_file.hpp"
#include "storage_codec.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace terrain::storage {

// The values of ByteWriter, little-endian, handed one byte at a time to Derived::put.
template <class Derived> class LittleWriter {
public:
    void bytes(std::span<const std::uint8_t> data) {
        for (const std::uint8_t byte : data) {
            self().put(byte);
        }
    }
    void u8(std::uint8_t value) { self().put(value); }
    void u16(std::uint16_t value) { little(value, 2); }
    void u32(std::uint32_t value) { little(value, 4); }
    void u64(std::uint64_t value) { little(value, 8); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void count(std::size_t value) { u32(static_cast<std::uint32_t>(value)); }

private:
    Derived& self() { return static_cast<Derived&>(*this); }
    void little(std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i) {
            self().put(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
        }
    }
};

// Counts the bytes written.
class ByteCounter : public LittleWriter<ByteCounter> {
public:
    void put(std::uint8_t) { ++size_; }
    [[nodiscard]] std::size_t size() const { return size_; }

private:
    std::size_t size_ = 0;
};

// Writes a frame of bodySize bytes of body: its header at once, the body as it comes (a buffer of
// kPiece bytes at a time), then its checksum with finish.
class FrameStream : public LittleWriter<FrameStream> {
public:
    static constexpr std::size_t kPiece = std::size_t{1} << 20;

    FrameStream(DurableWriter& out, const Magic& magic, std::uint32_t version,
                std::size_t bodySize)
        : out_(out), expected_(kFrameHeaderSize + bodySize) {
        buffer_.reserve(kPiece);
        LittleWriter::bytes(magic);
        u32(version);
        u64(static_cast<std::uint64_t>(kFrameSize + bodySize));
    }

    void put(std::uint8_t byte) {
        hash_ = (hash_ ^ byte) * 16777619U;
        buffer_.push_back(byte);
        ++written_;
        if (buffer_.size() == kPiece) {
            flush();
        }
    }

    // Writes the checksum of the frame. Throws std::logic_error if the body was not bodySize
    // bytes.
    void finish() {
        if (written_ != expected_) {
            throw std::logic_error("FrameStream: the body differs from its announced size");
        }
        for (int i = 0; i < 4; ++i) {
            buffer_.push_back(static_cast<std::uint8_t>((hash_ >> (8 * i)) & 0xFFU));
        }
        flush();
    }

private:
    void flush() {
        out_.write(buffer_);
        buffer_.clear();
    }

    DurableWriter& out_;
    std::size_t expected_;
    std::size_t written_ = 0;
    std::uint32_t hash_ = 2166136261U; // the FNV-1a of fnv1a (modifications.hpp)
    std::vector<std::uint8_t> buffer_;
};

} // namespace terrain::storage
