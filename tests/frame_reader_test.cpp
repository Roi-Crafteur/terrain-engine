// The frames read in pieces (src/storage/frame_reader.hpp): every value of the body back as
// written, across pieces of any size; the same errors as openFrame for every damage of the frame
// (a wrong checksum found at the end of the body, or before a failed read: the damage first);
// the file read whole by the end.
#include "frame_reader.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using terrain::StorageErrorKind;
using terrain::storage::ByteWriter;
using terrain::storage::FrameReader;

constexpr terrain::storage::Magic kMagic = {'T', 'E', 'S', 'T'};
constexpr std::uint32_t kVersion = 2;
constexpr std::size_t kValues = 3'000;

// A frame of kValues groups of values of every width, then 40 bytes taken whole.
std::vector<std::uint8_t> frameBytes() {
    ByteWriter out = terrain::storage::beginFrame(kMagic, kVersion, 0);
    for (std::size_t i = 0; i < kValues; ++i) {
        out.u8(static_cast<std::uint8_t>(i));
        out.u16(static_cast<std::uint16_t>(i * 7));
        out.u32(static_cast<std::uint32_t>(i * 100'003));
        out.u64(static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL);
        out.f32(static_cast<float>(i) * 0.25f);
        out.f64(static_cast<double>(i) / 3.0);
    }
    out.count(40);
    for (std::uint8_t i = 0; i < 40; ++i) {
        out.u8(static_cast<std::uint8_t>(200 + i));
    }
    return terrain::storage::sealFrame(std::move(out));
}

// Reads back the values of frameBytes; false at the first one unlike it.
bool readsBack(FrameReader& in) {
    bool same = in.version() == kVersion;
    for (std::size_t i = 0; i < kValues && same; ++i) {
        same = in.u8() == static_cast<std::uint8_t>(i) &&
               in.u16() == static_cast<std::uint16_t>(i * 7) &&
               in.u32() == static_cast<std::uint32_t>(i * 100'003) &&
               in.u64() == static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL &&
               in.f32() == static_cast<float>(i) * 0.25f &&
               in.f64() == static_cast<double>(i) / 3.0;
    }
    const std::size_t size = in.count(1);
    const std::span<const std::uint8_t> taken = in.take(size);
    for (std::size_t i = 0; i < taken.size() && same; ++i) {
        same = taken[i] == static_cast<std::uint8_t>(200 + i);
    }
    return same && size == 40 && in.remaining() == 0;
}

void testValues(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = frameBytes();
    storage_test::writeBytes(file, bytes);
    for (const std::size_t piece : {std::size_t{1}, std::size_t{7}, std::size_t{4'096},
                                    terrain::storage::kFramePiece}) {
        FrameReader in(file, kMagic, kVersion, "test", piece);
        TEST_CHECK(readsBack(in));
        in.expectEnd("bytes after the values");
        in.finish();
    }
    // A read past the end of the body fails truncated, with the file named.
    FrameReader in(file, kMagic, kVersion, "test", 7);
    (void)in.take(in.remaining());
    std::string message;
    TEST_CHECK(storage_test::errorOf([&] { (void)in.u8(); }, &message) ==
               StorageErrorKind::Invalid);
    TEST_CHECK(message == "test: truncated");
}

// What reading bytes throws, read in pieces of 7 to the end, and what openFrame throws.
std::optional<StorageErrorKind> streamed(const std::filesystem::path& file,
                                         const std::vector<std::uint8_t>& bytes) {
    storage_test::writeBytes(file, bytes);
    return storage_test::errorOf([&] {
        FrameReader in(file, kMagic, kVersion, "test", 7);
        (void)in.take(in.remaining());
        in.finish();
    });
}

std::optional<StorageErrorKind> whole(const std::vector<std::uint8_t>& bytes) {
    return storage_test::errorOf(
        [&] { (void)terrain::storage::openFrame(bytes, kMagic, kVersion, "test"); });
}

void testDamage(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = frameBytes();
    std::vector<std::vector<std::uint8_t>> damaged;
    std::vector<std::uint8_t> b = bytes;
    b[bytes.size() / 2] ^= 0x10; // a byte of the body
    damaged.push_back(b);
    b = bytes;
    b.resize(bytes.size() / 3);
    damaged.push_back(b);
    damaged.push_back({bytes.begin(), bytes.begin() + 10});
    b = bytes;
    b.push_back(0);
    damaged.push_back(b);
    for (const std::uint32_t version : {0U, kVersion + 1}) {
        b = bytes;
        storage_test::putU32(b, 4, version);
        damaged.push_back(b);
        damaged.push_back(storage_test::resealed(b));
    }
    b = bytes;
    b[0] = 'X';
    damaged.push_back(b);
    damaged.push_back(storage_test::resealed(b));
    for (const std::vector<std::uint8_t>& d : damaged) {
        TEST_CHECK(whole(d).has_value());
        TEST_CHECK(streamed(file, d) == whole(d));
    }
    TEST_CHECK(streamed(file, bytes) == std::nullopt);
}

// A read that fails with a wrong checksum: the damage (Corrupt), as openFrame would tell before
// any read; with the right checksum, what the read found (Invalid).
void testFailedRead(const std::filesystem::path& file) {
    std::vector<std::uint8_t> bytes = frameBytes();
    bytes[bytes.size() - 10] ^= 0x01; // in the bytes taken whole: never read before the failure
    for (const bool sealed : {false, true}) {
        storage_test::writeBytes(file, sealed ? storage_test::resealed(bytes) : bytes);
        FrameReader in(file, kMagic, kVersion, "test", 7);
        TEST_CHECK(storage_test::errorOf([&] { in.fail("an unexpected value"); }) ==
                   (sealed ? StorageErrorKind::Invalid : StorageErrorKind::Corrupt));
    }
}

} // namespace

int main() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_frame_reader_test");
    const std::filesystem::path file = dir / "frame.bin";
    testValues(file);
    testDamage(file);
    testFailedRead(file);
    TEST_CHECK(storage_test::errorOf([&] {
                   const FrameReader in(dir / "missing.bin", kMagic, kVersion, "test");
                   (void)in.version();
               }) == StorageErrorKind::Io);
    std::filesystem::remove_all(dir);
    return test::result();
}
