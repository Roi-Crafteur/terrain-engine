// World metadata (world.meta): round trip, deterministic and pinned bytes, field layout, damaged
// files (every truncation, every flipped byte), versions, unknown required and optional fields,
// missing or repeated fields, validation of names, sizes, parameters and spawn point, and the
// warning of a changed generator.
#include "terrain/storage/world_meta.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using storage_test::errorOf;
using terrain::StorageErrorKind;
using terrain::WorldMeta;

struct Field {
    std::uint16_t tag = 0;
    std::vector<std::uint8_t> payload;
};

// The fields of a world.meta, in file order.
std::vector<Field> fieldsOf(const std::vector<std::uint8_t>& bytes) {
    std::vector<Field> fields;
    std::size_t at = 16;
    while (at + 4 < bytes.size()) {
        Field field;
        field.tag = static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        std::uint32_t size = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            size |= static_cast<std::uint32_t>(bytes[at + 2 + i]) << (8 * i);
        }
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(at + 6);
        field.payload.assign(begin, begin + size);
        fields.push_back(field);
        at += 6 + size;
    }
    return fields;
}

// A sealed world.meta of these fields.
std::vector<std::uint8_t> withFields(const std::vector<Field>& fields, std::uint32_t version = 1) {
    std::vector<std::uint8_t> bytes = {'T', 'W', 'L', 'D', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    storage_test::putU32(bytes, 4, version);
    for (const Field& field : fields) {
        bytes.push_back(static_cast<std::uint8_t>(field.tag & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(field.tag >> 8));
        bytes.resize(bytes.size() + 4);
        storage_test::putU32(bytes, bytes.size() - 4,
                             static_cast<std::uint32_t>(field.payload.size()));
        bytes.insert(bytes.end(), field.payload.begin(), field.payload.end());
    }
    bytes.resize(bytes.size() + 4);
    return storage_test::resealed(bytes);
}

std::optional<StorageErrorKind> readError(const std::vector<std::uint8_t>& bytes,
                                          std::string* message = nullptr) {
    return errorOf([&bytes] { static_cast<void>(terrain::deserializeWorldMeta(bytes)); },
                   message);
}

std::optional<StorageErrorKind> writeError(const WorldMeta& meta) {
    return errorOf([&meta] { static_cast<void>(terrain::serializeWorldMeta(meta)); });
}

void testRoundTrip() {
    const WorldMeta meta = storage_test::sampleMeta();
    const std::vector<std::uint8_t> bytes = terrain::serializeWorldMeta(meta);
    const WorldMeta read = terrain::deserializeWorldMeta(bytes);
    TEST_CHECK(read == meta);
    TEST_CHECK(terrain::serializeWorldMeta(read) == bytes);
    TEST_CHECK(terrain::serializeWorldMeta(storage_test::sampleMeta()) == bytes); // deterministic

    WorldMeta minimal;
    minimal.name = "a";
    TEST_CHECK(terrain::deserializeWorldMeta(terrain::serializeWorldMeta(minimal)) == minimal);
}

// The layout of the fields, then the bytes of the whole file: identical on every platform.
void testPinnedBytes() {
    WorldMeta meta = storage_test::sampleMeta();
    meta.generatorVersion = 1; // fixed: the pinned bytes must not follow kGeneratorVersion
    const std::vector<std::uint8_t> bytes = terrain::serializeWorldMeta(meta);
    TEST_CHECK(std::vector(bytes.begin(), bytes.begin() + 8) ==
               (std::vector<std::uint8_t>{'T', 'W', 'L', 'D', 1, 0, 0, 0}));
    TEST_CHECK(storage_test::getU64(bytes, 8) == bytes.size());
    const std::vector<Field> fields = fieldsOf(bytes);
    const std::vector<std::size_t> sizes = {
        meta.name.size(), 8, 16, 4, 4 + (1 + 27 + 8) + (1 + 16 + 8), 8, 8, 24, 8};
    TEST_CHECK(fields.size() == sizes.size());
    for (std::size_t i = 0; i < fields.size() && i < sizes.size(); ++i) {
        TEST_CHECK(static_cast<std::size_t>(fields[i].tag) == i + 1 &&
                   fields[i].payload.size() == sizes[i]);
    }
    TEST_CHECK(fields[1].payload ==
               (std::vector<std::uint8_t>{0x9B, 0x28, 0x35, 0x01, 0, 0, 0, 0})); // the seed
    TEST_CHECK(bytes.size() == 233);
    TEST_CHECK(terrain::fnv1a(bytes) == 0xA2B3ADC6U);
}

void testDefaults() {
    TEST_CHECK(terrain::kDefaultWorldSizeX == 60'030'976U);
    TEST_CHECK(terrain::kDefaultWorldSizeZ == 34'996'224U);
    const WorldMeta meta;
    TEST_CHECK(meta.sizeX % terrain::kWorldSizeStep == 0 &&
               meta.sizeZ % terrain::kWorldSizeStep == 0);
    TEST_CHECK(meta.generatorVersion == terrain::kGeneratorVersion);
    TEST_CHECK(terrain::unixNow() > 1'700'000'000);
}

// A damaged file is always reported as damaged, never read and never a crash.
void testDamagedFiles() {
    const std::vector<std::uint8_t> bytes =
        terrain::serializeWorldMeta(storage_test::sampleMeta());
    bool truncated = true;
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const std::vector<std::uint8_t> part(bytes.begin(),
                                             bytes.begin() + static_cast<std::ptrdiff_t>(length));
        truncated = truncated && readError(part) == StorageErrorKind::Truncated;
    }
    TEST_CHECK(truncated);
    bool flipped = true;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::vector<std::uint8_t> damaged = bytes;
        damaged[i] ^= 0x10U;
        flipped = flipped && storage_test::isDamage(readError(damaged));
    }
    TEST_CHECK(flipped);
    std::vector<std::uint8_t> longer = bytes;
    longer.push_back(0);
    TEST_CHECK(readError(longer) == StorageErrorKind::Corrupt);
}

std::vector<Field> sampleFields() {
    return fieldsOf(terrain::serializeWorldMeta(storage_test::sampleMeta()));
}

void testVersions() {
    const std::vector<Field> fields = sampleFields();
    TEST_CHECK(terrain::deserializeWorldMeta(withFields(fields)) == storage_test::sampleMeta());
    std::string message;
    TEST_CHECK(readError(withFields(fields, 2), &message) == StorageErrorKind::NewerVersion);
    TEST_CHECK(message.find("newer version") != std::string::npos);
    TEST_CHECK(readError(withFields(fields, 0)) == StorageErrorKind::Invalid);
    std::vector<std::uint8_t> other = withFields(fields);
    other[3] = 'X';
    TEST_CHECK(readError(storage_test::resealed(other), &message) == StorageErrorKind::Invalid);
    TEST_CHECK(message.find("not a world.meta") != std::string::npos);
}

void testUnknownFields() {
    const std::vector<std::uint8_t> bytes =
        terrain::serializeWorldMeta(storage_test::sampleMeta());
    const std::vector<Field> fields = fieldsOf(bytes);
    TEST_CHECK(withFields(fields) == bytes);

    // A required field of a newer version: refused.
    std::vector<Field> required = fields;
    required.push_back({0x000A, {1, 2}});
    std::string message;
    TEST_CHECK(readError(withFields(required), &message) == StorageErrorKind::Invalid);
    TEST_CHECK(message.find("unknown field 0x000A") != std::string::npos);

    // An optional field: kept unread, written back as it was.
    std::vector<Field> optional = fields;
    optional.push_back({0x8001, {1, 2, 3}});
    optional.push_back({0xC000, {}});
    const std::vector<std::uint8_t> withOptional = withFields(optional);
    const WorldMeta read = terrain::deserializeWorldMeta(withOptional);
    TEST_CHECK(read.unknownOptionalFields.size() == 2);
    TEST_CHECK(read.unknownOptionalFields.at(0x8001) == (std::vector<std::uint8_t>{1, 2, 3}));
    TEST_CHECK(read.unknownOptionalFields.at(0xC000).empty());
    WorldMeta withoutOptional = read;
    withoutOptional.unknownOptionalFields.clear();
    TEST_CHECK(withoutOptional == storage_test::sampleMeta());
    TEST_CHECK(terrain::serializeWorldMeta(read) == withOptional);
    // A field before the optional ones, or the optional field repeated: out of order.
    std::vector<Field> misplaced = optional;
    misplaced.push_back({0x0009, std::vector<std::uint8_t>(8, 0)});
    TEST_CHECK(readError(withFields(misplaced)) == StorageErrorKind::Invalid);
    std::vector<Field> repeated = optional;
    repeated.push_back(optional.back());
    TEST_CHECK(readError(withFields(repeated)) == StorageErrorKind::Invalid);
}

void testFieldErrors() {
    const std::vector<Field> fields = sampleFields();
    std::string message;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        std::vector<Field> missing = fields;
        missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(i));
        TEST_CHECK(readError(withFields(missing), &message) == StorageErrorKind::Invalid);
        TEST_CHECK(message.find("missing field") != std::string::npos);
        std::vector<Field> longer = fields;
        longer[i].payload.push_back(0);
        TEST_CHECK(readError(withFields(longer)) == StorageErrorKind::Invalid);
        if (i != 0) { // a shorter name is still a name
            std::vector<Field> shorter = fields;
            shorter[i].payload.pop_back();
            TEST_CHECK(readError(withFields(shorter)) == StorageErrorKind::Invalid);
        }
    }
    std::vector<Field> swapped = fields;
    std::swap(swapped[1], swapped[2]);
    TEST_CHECK(readError(withFields(swapped)) == StorageErrorKind::Invalid);
    std::vector<Field> twice = fields;
    twice.insert(twice.begin() + 2, fields[1]);
    TEST_CHECK(readError(withFields(twice)) == StorageErrorKind::Invalid);
    // A spawn point outside the world, read from a file: refused like a written one.
    std::vector<Field> outside = fields;
    storage_test::putU64(outside[7].payload, 0, std::bit_cast<std::uint64_t>(4.0e7));
    TEST_CHECK(readError(withFields(outside), &message) == StorageErrorKind::Invalid);
    TEST_CHECK(message.find("spawn") != std::string::npos);
}

void testNames() {
    TEST_CHECK(terrain::isValidWorldName("Mon monde"));
    TEST_CHECK(terrain::isValidWorldName(storage_test::sampleMeta().name));
    TEST_CHECK(terrain::isValidWorldName("\xF0\x9F\x8C\x8D Terre"));  // an emoji
    TEST_CHECK(terrain::isValidWorldName(std::string(128, 'a')));
    TEST_CHECK(!terrain::isValidWorldName(""));
    TEST_CHECK(!terrain::isValidWorldName(std::string(129, 'a')));
    for (const char* bad : {"tab\there", "del\x7F", "c1\xC2\x85", "cut\xC3", "bad\xC3\x28",
                            "overlong\xC0\xAF", "surrogate\xED\xA0\x80", "big\xF4\x90\x80\x80",
                            "lone\x80"}) {
        TEST_CHECK(!terrain::isValidWorldName(bad));
    }
    TEST_CHECK(!terrain::isValidWorldName(std::string("nul\0x", 5)));
}

void testValidation() {
    const WorldMeta good = storage_test::sampleMeta();
    TEST_CHECK(!writeError(good));
    const auto invalid = [&good](auto change) {
        WorldMeta meta = good;
        change(meta);
        return writeError(meta) == StorageErrorKind::Invalid;
    };
    TEST_CHECK(invalid([](WorldMeta& m) { m.name.clear(); }));
    TEST_CHECK(invalid([](WorldMeta& m) { m.sizeX = 0; }));
    TEST_CHECK(invalid([](WorldMeta& m) { m.sizeZ = 3 * terrain::kWorldSizeStep + 16; }));
    TEST_CHECK(invalid([](WorldMeta& m) {
        m.sizeX = terrain::kMaxWorldSize + terrain::kWorldSizeStep;
    }));
    TEST_CHECK(invalid([](WorldMeta& m) { m.generation["Bad"] = 1.0; }));
    TEST_CHECK(invalid([](WorldMeta& m) { m.generation[std::string(65, 'a')] = 1.0; }));
    TEST_CHECK(invalid([](WorldMeta& m) {
        m.generation["inf"] = std::numeric_limits<double>::infinity();
    }));
    TEST_CHECK(invalid([](WorldMeta& m) { m.spawn.y = std::nan(""); }));
    TEST_CHECK(invalid([](WorldMeta& m) { m.unknownOptionalFields[0x0042] = {}; }));
    // The world is centered: [-W / 2, W / 2) and [-H / 2, H / 2).
    const double halfX = static_cast<double>(good.sizeX) / 2.0;
    const double halfZ = static_cast<double>(good.sizeZ) / 2.0;
    TEST_CHECK(invalid([halfX](WorldMeta& m) { m.spawn.x = halfX; }));
    TEST_CHECK(invalid([halfZ](WorldMeta& m) { m.spawn.z = halfZ; }));
    TEST_CHECK(invalid([halfZ](WorldMeta& m) { m.spawn.z = -halfZ - 1.0; }));
    WorldMeta corner = good;
    corner.spawn = {-halfX, -1000.0, std::nextafter(halfZ, 0.0)};
    TEST_CHECK(!writeError(corner));
    WorldMeta largest = good;
    largest.sizeX = terrain::kMaxWorldSize;
    TEST_CHECK(!writeError(largest));
}

void testGeneratorWarning() {
    WorldMeta meta = storage_test::sampleMeta();
    TEST_CHECK(!terrain::generatorChanged(meta) && terrain::generatorWarning(meta).empty());
    meta.generatorVersion = terrain::kGeneratorVersion + 1;
    TEST_CHECK(terrain::generatorChanged(meta));
    const std::string warning = terrain::generatorWarning(meta);
    TEST_CHECK(warning.find(std::to_string(terrain::kGeneratorVersion + 1)) != std::string::npos);
    TEST_CHECK(terrain::deserializeWorldMeta(terrain::serializeWorldMeta(meta)) == meta);
}

} // namespace

int main() {
    testRoundTrip();
    testPinnedBytes();
    testDefaults();
    testDamagedFiles();
    testVersions();
    testUnknownFields();
    testFieldErrors();
    testNames();
    testValidation();
    testGeneratorWarning();
    return test::result();
}
