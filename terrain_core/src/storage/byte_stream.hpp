#pragma once

// Little-endian bytes of the storage files, shared by the files of src/storage only. The values
// are written byte by byte and floats through their IEEE bits, so every platform writes and
// reads the same bytes.

#include "terrain/storage/storage_error.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace terrain::storage {

class ByteWriter {
public:
    explicit ByteWriter(std::size_t capacity = 0) { out_.reserve(capacity); }

    void bytes(std::span<const std::uint8_t> data) {
        out_.insert(out_.end(), data.begin(), data.end());
    }
    void u8(std::uint8_t value) { out_.push_back(value); }
    void u16(std::uint16_t value) { little(value, 2); }
    void u32(std::uint32_t value) { little(value, 4); }
    void u64(std::uint64_t value) { little(value, 8); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void count(std::size_t value) { u32(static_cast<std::uint32_t>(value)); }
    // Writes value over the 8 bytes at offset, already written.
    void patchU64(std::size_t offset, std::uint64_t value) {
        for (std::size_t i = 0; i < 8; ++i) {
            out_[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
        }
    }
    [[nodiscard]] std::size_t size() const { return out_.size(); }
    [[nodiscard]] std::span<const std::uint8_t> written() const { return out_; }
    std::vector<std::uint8_t> take() { return std::move(out_); }

private:
    void little(std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i) {
            out_.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
        }
    }

    std::vector<std::uint8_t> out_;
};

// Reads the values written by a ByteWriter. Every failure throws a StorageError (Invalid) whose
// message is the prefix of the reader followed by what is wrong.
class ByteReader {
public:
    ByteReader(std::span<const std::uint8_t> data, std::string prefix)
        : data_(data), prefix_(std::move(prefix)) {}

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
    // The next size bytes.
    std::span<const std::uint8_t> take(std::size_t size) {
        if (size > remaining()) {
            fail("truncated");
        }
        const std::span<const std::uint8_t> part = data_.subspan(position_, size);
        position_ += size;
        return part;
    }
    // A reader of the next size bytes, whose messages start with prefix.
    ByteReader sub(std::size_t size, const std::string& prefix) {
        return {take(size), prefix_ + prefix};
    }
    [[nodiscard]] std::size_t remaining() const { return data_.size() - position_; }
    // Fails with what unless every byte was read.
    void expectEnd(const char* what) const {
        if (remaining() != 0) {
            fail(what);
        }
    }
    [[noreturn]] void fail(const std::string& what) const {
        throw StorageError(StorageErrorKind::Invalid, prefix_ + what);
    }

private:
    std::uint64_t little(int size) {
        if (remaining() < static_cast<std::size_t>(size)) {
            fail("truncated");
        }
        std::uint64_t value = 0;
        for (int i = 0; i < size; ++i) {
            value |= static_cast<std::uint64_t>(data_[position_++]) << (8 * i);
        }
        return value;
    }
    template <class T>
    T finite(T value) const {
        if (!std::isfinite(value)) {
            fail("non-finite value");
        }
        return value;
    }

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
    std::string prefix_;
};

} // namespace terrain::storage
