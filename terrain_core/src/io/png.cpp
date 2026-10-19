#include "terrain/io/png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <stdexcept>

namespace terrain {

namespace {

constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

// Largest payload of a stored deflate block.
constexpr std::size_t kMaxStoredBlock = 65535;

constexpr std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n) {
        std::uint32_t c = n;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
        }
        table[n] = c;
    }
    return table;
}
constexpr std::array<std::uint32_t, 256> kCrcTable = makeCrcTable();

void appendU16LittleEndian(std::vector<std::uint8_t>& out, std::size_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

void appendU32BigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

// Chunk layout: length, type, data, CRC-32 of type and data.
void appendChunk(std::vector<std::uint8_t>& out, const char (&type)[5],
                 std::span<const std::uint8_t> data) {
    appendU32BigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const auto typeAndData = std::span(out).subspan(typeStart);
    appendU32BigEndian(out, png::crc32(typeAndData));
}

// Scanlines, each preceded by filter type 0 (none).
std::vector<std::uint8_t> scanlines(const Image& image) {
    const auto rowBytes = static_cast<std::size_t>(image.width * image.channels);
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * static_cast<std::size_t>(image.height));
    for (std::size_t row = 0; row < static_cast<std::size_t>(image.height); ++row) {
        raw.push_back(0);
        const auto begin = image.pixels.begin() + static_cast<std::ptrdiff_t>(row * rowBytes);
        raw.insert(raw.end(), begin, begin + static_cast<std::ptrdiff_t>(rowBytes));
    }
    return raw;
}

// zlib stream (deflate, 32 KiB window, no dictionary) made of stored blocks.
std::vector<std::uint8_t> zlibStored(std::span<const std::uint8_t> raw) {
    std::vector<std::uint8_t> out = {0x78, 0x01};
    std::size_t offset = 0;
    do {
        const std::size_t length = std::min(kMaxStoredBlock, raw.size() - offset);
        const bool last = offset + length == raw.size();
        out.push_back(static_cast<std::uint8_t>(last ? 1 : 0)); // BFINAL, BTYPE 00 (stored)
        appendU16LittleEndian(out, length);
        appendU16LittleEndian(out, ~length & 0xFFFFU);
        const auto block = raw.subspan(offset, length);
        out.insert(out.end(), block.begin(), block.end());
        offset += length;
    } while (offset < raw.size());
    appendU32BigEndian(out, png::adler32(raw));
    return out;
}

void validate(const Image& image) {
    if (image.width <= 0 || image.height <= 0) {
        throw std::invalid_argument("encodePng: empty image");
    }
    if (image.channels != 1 && image.channels != 3) {
        throw std::invalid_argument("encodePng: only gray (1) and RGB (3) images are supported");
    }
    const auto expected = static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height) *
                          static_cast<std::size_t>(image.channels);
    if (image.pixels.size() != expected) {
        throw std::invalid_argument("encodePng: pixel count does not match the image size");
    }
}

} // namespace

namespace png {

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc = kCrcTable[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t adler32(std::span<const std::uint8_t> bytes) {
    constexpr std::uint32_t kModulus = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : bytes) {
        a = (a + byte) % kModulus;
        b = (b + a) % kModulus;
    }
    return (b << 16) | a;
}

} // namespace png

std::vector<std::uint8_t> encodePng(const Image& image) {
    validate(image);
    std::vector<std::uint8_t> header;
    appendU32BigEndian(header, static_cast<std::uint32_t>(image.width));
    appendU32BigEndian(header, static_cast<std::uint32_t>(image.height));
    const auto colorType = static_cast<std::uint8_t>(image.channels == 1 ? 0 : 2); // gray, RGB
    // Bit depth 8, color type, deflate compression, adaptive filtering, no interlace.
    header.insert(header.end(), {8, colorType, 0, 0, 0});

    std::vector<std::uint8_t> out(kSignature.begin(), kSignature.end());
    appendChunk(out, "IHDR", header);
    appendChunk(out, "IDAT", zlibStored(scanlines(image)));
    appendChunk(out, "IEND", {});
    return out;
}

bool writePng(const std::filesystem::path& path, const Image& image) {
    const std::vector<std::uint8_t> bytes = encodePng(image);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace terrain
