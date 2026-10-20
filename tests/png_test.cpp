#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using terrain::Image;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t kSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

std::span<const std::uint8_t> asBytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::uint32_t readU32(const Bytes& bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
}

struct Chunk {
    std::string type;
    Bytes data;
    bool crcValid = false;
};

// Chunks following the signature; stops at the first truncated chunk.
std::vector<Chunk> parseChunks(const Bytes& file) {
    std::vector<Chunk> chunks;
    std::size_t at = sizeof(kSignature);
    while (at + 12 <= file.size()) {
        const std::size_t length = readU32(file, at);
        if (at + 12 + length > file.size()) {
            break;
        }
        const auto begin = file.begin() + static_cast<std::ptrdiff_t>(at);
        Chunk chunk{std::string(begin + 4, begin + 8),
                    Bytes(begin + 8, begin + 8 + static_cast<std::ptrdiff_t>(length)), false};
        const auto typeAndData = std::span(file).subspan(at + 4, length + 4);
        chunk.crcValid = terrain::png::crc32(typeAndData) == readU32(file, at + 8 + length);
        chunks.push_back(chunk);
        at += 12 + length;
    }
    return chunks;
}

// Inflates a zlib stream made only of stored deflate blocks, checking every length,
// complement and the Adler-32 trailer. Counts the blocks.
std::optional<Bytes> inflateStored(const Bytes& stream, int& blocks) {
    blocks = 0;
    if (stream.size() < 6 || stream[0] != 0x78 || ((stream[0] << 8) | stream[1]) % 31 != 0) {
        return std::nullopt;
    }
    Bytes out;
    std::size_t at = 2;
    bool final = false;
    while (!final) {
        if (at + 5 > stream.size() || (stream[at] >> 1) != 0) { // BTYPE 00, padding bits
            return std::nullopt;
        }
        final = (stream[at] & 1U) != 0;
        const auto length = static_cast<std::size_t>(stream[at + 1] | (stream[at + 2] << 8));
        const auto complement = static_cast<std::size_t>(stream[at + 3] | (stream[at + 4] << 8));
        if ((length ^ 0xFFFFU) != complement || at + 5 + length > stream.size()) {
            return std::nullopt;
        }
        const auto begin = stream.begin() + static_cast<std::ptrdiff_t>(at + 5);
        out.insert(out.end(), begin, begin + static_cast<std::ptrdiff_t>(length));
        at += 5 + length;
        ++blocks;
    }
    if (at + 4 != stream.size() || readU32(stream, at) != terrain::png::adler32(out)) {
        return std::nullopt;
    }
    return out;
}

// Decodes a PNG written by encodePng: checks the structure and returns the scanlines.
std::optional<Bytes> decode(const Bytes& file, const Image& expected, int& blocks) {
    if (file.size() < sizeof(kSignature) || !std::equal(std::begin(kSignature),
                                                        std::end(kSignature), file.begin())) {
        return std::nullopt;
    }
    const std::vector<Chunk> chunks = parseChunks(file);
    if (chunks.size() != 3 || chunks[0].type != "IHDR" || chunks[1].type != "IDAT" ||
        chunks[2].type != "IEND" || !chunks[2].data.empty()) {
        return std::nullopt;
    }
    for (const Chunk& chunk : chunks) {
        if (!chunk.crcValid) {
            return std::nullopt;
        }
    }
    const Bytes& header = chunks[0].data;
    const auto colorType = static_cast<std::uint8_t>(expected.channels == 1 ? 0 : 2);
    const Bytes expectedTail = {8, colorType, 0, 0, 0};
    if (header.size() != 13 || readU32(header, 0) != static_cast<std::uint32_t>(expected.width) ||
        readU32(header, 4) != static_cast<std::uint32_t>(expected.height) ||
        !std::equal(expectedTail.begin(), expectedTail.end(), header.begin() + 8)) {
        return std::nullopt;
    }
    return inflateStored(chunks[1].data, blocks);
}

// Rows of the image, each preceded by filter type 0.
Bytes expectedScanlines(const Image& image) {
    const auto rowBytes = static_cast<std::ptrdiff_t>(image.width * image.channels);
    Bytes raw;
    for (int row = 0; row < image.height; ++row) {
        raw.push_back(0);
        const auto begin = image.pixels.begin() + row * rowBytes;
        raw.insert(raw.end(), begin, begin + rowBytes);
    }
    return raw;
}

bool rejects(const Image& image) {
    try {
        (void)terrain::encodePng(image);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

void testChecksums() {
    TEST_CHECK(terrain::png::crc32(asBytes("123456789")) == 0xCBF43926U);
    TEST_CHECK(terrain::png::crc32(asBytes("IEND")) == 0xAE426082U);
    TEST_CHECK(terrain::png::crc32(asBytes("")) == 0U);
    TEST_CHECK(terrain::png::adler32(asBytes("Wikipedia")) == 0x11E60398U);
    TEST_CHECK(terrain::png::adler32(asBytes("")) == 1U);
}

void testGrayImage() {
    const Image image{3, 2, 1, {0, 50, 100, 150, 200, 250}};
    int blocks = 0;
    const std::optional<Bytes> raw = decode(terrain::encodePng(image), image, blocks);
    TEST_CHECK(raw.has_value());
    TEST_CHECK(raw == Bytes({0, 0, 50, 100, 0, 150, 200, 250}));
    TEST_CHECK(blocks == 1);
}

void testRgbImageSpansSeveralBlocks() {
    Image image{200, 120, 3, {}};
    for (int j = 0; j < image.height; ++j) {
        for (int i = 0; i < image.width; ++i) {
            for (int c = 0; c < 3; ++c) {
                const int value = (i * 7 + j * 13 + c * 101) & 0xFF;
                image.pixels.push_back(static_cast<std::uint8_t>(value));
            }
        }
    }
    int blocks = 0;
    const std::optional<Bytes> raw = decode(terrain::encodePng(image), image, blocks);
    TEST_CHECK(raw == expectedScanlines(image));
    TEST_CHECK(blocks == 2); // 72120 bytes of scanlines, at most 65535 per stored block
}

void testInvalidImages() {
    TEST_CHECK(rejects(Image{0, 4, 1, {}}));
    TEST_CHECK(rejects(Image{2, 2, 2, Bytes(8, 0)}));
    TEST_CHECK(rejects(Image{2, 2, 4, Bytes(16, 0)}));
    TEST_CHECK(rejects(Image{2, 2, 3, Bytes(11, 0)}));
    TEST_CHECK(!rejects(Image{2, 2, 3, Bytes(12, 0)}));
}

void testWritePng() {
    const Image image{4, 4, 1, Bytes(16, 128)};
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "terrain_png_test.png";
    TEST_CHECK(terrain::writePng(path, image));
    std::ifstream file(path, std::ios::binary);
    const Bytes written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    TEST_CHECK(written == terrain::encodePng(image));
    file.close();
    std::filesystem::remove(path);

    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "terrain_png_test_missing_dir" / "image.png";
    TEST_CHECK(!terrain::writePng(missing, image));
}

} // namespace

int main() {
    testChecksums();
    testGrayImage();
    testRgbImageSpansSeveralBlocks();
    testInvalidImages();
    testWritePng();
    return test::result();
}
