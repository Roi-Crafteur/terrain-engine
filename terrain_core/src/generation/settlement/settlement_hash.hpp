#pragma once

// Internal to the settlement: a 64-bit FNV-1a over the raw bits of values, in the order they are
// added: the checksums of the maps and of the kingdoms.

#include <bit>
#include <cstdint>
#include <span>
#include <type_traits>

namespace terrain::settlement {

class Fnv64 {
public:
    void addByte(std::uint8_t b) {
        hash_ ^= b;
        hash_ *= 0x100000001B3ULL;
    }
    void add(std::uint64_t v, int bytes = 8) {
        for (int k = 0; k < bytes; ++k) {
            addByte(static_cast<std::uint8_t>(v >> (8 * k)));
        }
    }
    void add(double v) { add(std::bit_cast<std::uint64_t>(v)); }
    void add(float v) { add(std::bit_cast<std::uint32_t>(v), 4); }
    void addBytes(std::span<const std::uint8_t> bytes) {
        for (const std::uint8_t b : bytes) {
            addByte(b);
        }
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

} // namespace terrain::settlement
