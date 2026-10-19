#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace terrain {

// 8-bit image, rows from top to bottom, channels interleaved: 1 = gray, 3 = RGB.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 1;
    std::vector<std::uint8_t> pixels; // width * height * channels bytes
};

// Bytes of a PNG file holding image, without compression (zlib stream of stored deflate
// blocks), so it needs no third-party library. Throws std::invalid_argument if the image
// is empty, has 0, 2 or more than 3 channels, or its pixel count does not match its size.
[[nodiscard]] std::vector<std::uint8_t> encodePng(const Image& image);

// Writes encodePng(image) to path. Returns false if the file cannot be written.
[[nodiscard]] bool writePng(const std::filesystem::path& path, const Image& image);

namespace png {

// Checksums used by PNG chunks (CRC-32, ISO 3309) and zlib streams (Adler-32).
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::uint32_t adler32(std::span<const std::uint8_t> bytes);

} // namespace png

} // namespace terrain
