#include "terrain/storage/world_meta.hpp"

#include "byte_stream.hpp"
#include "storage_codec.hpp"
#include "terrain/storage/storage_error.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace terrain {

namespace {

using storage::ByteReader;
using storage::ByteWriter;

constexpr storage::Magic kMagic = {'T', 'W', 'L', 'D'};

// The required fields of format version 1, by tag.
enum Field : std::uint16_t {
    kName = 1,
    kSeed,
    kSize,
    kGenerator,
    kGeneration,
    kCreated,
    kSaved,
    kSpawn,
    kPlayTime,
};
constexpr std::array<const char*, kPlayTime + 1> kFieldNames = {
    "", "name", "seed", "size", "generator version", "generation", "created", "saved", "spawn",
    "play time"};
// A generation parameter: its name length, at least one character, and its value.
constexpr std::size_t kMinParameterRecord = 10;

[[noreturn]] void invalid(const std::string& what) {
    throw StorageError(StorageErrorKind::Invalid, "invalid world metadata: " + what);
}

std::string tagText(std::uint16_t tag) {
    constexpr const char* kDigits = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 12; shift >= 0; shift -= 4) {
        text += kDigits[(tag >> shift) & 0xF];
    }
    return text;
}

// Code point of the UTF-8 sequence at text[i] and its length; nullopt if the sequence is
// invalid, overlong, a surrogate or beyond U+10FFFF.
std::optional<std::uint32_t> decodeUtf8(std::string_view text, std::size_t i,
                                        std::size_t& length) {
    const auto lead = static_cast<std::uint8_t>(text[i]);
    if (lead < 0x80U) {
        length = 1;
        return lead;
    }
    length = lead >= 0xF0U ? 4 : lead >= 0xE0U ? 3 : lead >= 0xC2U ? 2 : 0;
    if (length == 0 || lead > 0xF4U || i + length > text.size()) {
        return std::nullopt;
    }
    std::uint32_t code = lead & (0x7FU >> length);
    for (std::size_t k = 1; k < length; ++k) {
        const auto next = static_cast<std::uint8_t>(text[i + k]);
        if ((next & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        code = (code << 6) | (next & 0x3FU);
    }
    constexpr std::array<std::uint32_t, 5> kMinimum = {0, 0, 0x80, 0x800, 0x10000};
    if (code < kMinimum[length] || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) {
        return std::nullopt;
    }
    return code;
}

bool isValidKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxSettingKeyBytes) {
        return false;
    }
    for (const char c : key) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

// v in [-size / 2, size / 2): sizes are exact in a double.
bool inWorld(double v, std::uint64_t size) {
    const double half = static_cast<double>(size) / 2.0;
    return std::isfinite(v) && v >= -half && v < half;
}

std::span<const std::uint8_t> textBytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::string text(std::span<const std::uint8_t> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void field(ByteWriter& out, std::uint16_t tag, const ByteWriter& payload) {
    out.u16(tag);
    out.count(payload.size());
    out.bytes(payload.written());
}

ByteWriter generationPayload(const std::map<std::string, double>& generation) {
    ByteWriter out;
    out.count(generation.size());
    for (const auto& [key, value] : generation) {
        out.u8(static_cast<std::uint8_t>(key.size()));
        out.bytes(textBytes(key));
        out.f64(value);
    }
    return out;
}

void readGeneration(ByteReader& in, std::map<std::string, double>& generation) {
    const std::size_t count = in.count(kMinParameterRecord);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t length = in.u8();
        std::string key = text(in.take(length));
        if (!generation.empty() && !(generation.rbegin()->first < key)) {
            in.fail("parameters out of order or repeated");
        }
        const double value = in.f64();
        generation.emplace_hint(generation.end(), std::move(key), value);
    }
}

void readField(ByteReader& in, std::uint16_t tag, WorldMeta& meta) {
    switch (tag) {
    case kName:
        meta.name = text(in.take(in.remaining()));
        break;
    case kSeed:
        meta.seed = in.u64();
        break;
    case kSize:
        meta.sizeX = in.u64();
        meta.sizeZ = in.u64();
        break;
    case kGenerator:
        meta.generatorVersion = in.u32();
        break;
    case kGeneration:
        readGeneration(in, meta.generation);
        break;
    case kCreated:
        meta.createdAt = in.i64();
        break;
    case kSaved:
        meta.savedAt = in.i64();
        break;
    case kSpawn:
        meta.spawn.x = in.f64();
        meta.spawn.y = in.f64();
        meta.spawn.z = in.f64();
        break;
    default: // kPlayTime, the last one
        meta.playTimeSeconds = in.u64();
        break;
    }
}

} // namespace

bool isValidWorldName(std::string_view name) {
    if (name.empty() || name.size() > kMaxWorldNameBytes) {
        return false;
    }
    for (std::size_t i = 0; i < name.size();) {
        std::size_t length = 0;
        const std::optional<std::uint32_t> code = decodeUtf8(name, i, length);
        // Control characters: C0, DEL and C1.
        if (!code || *code < 0x20U || (*code >= 0x7FU && *code < 0xA0U)) {
            return false;
        }
        i += length;
    }
    return true;
}

void validateWorldMeta(const WorldMeta& meta) {
    if (!isValidWorldName(meta.name)) {
        invalid("the name must be 1 to 128 bytes of UTF-8 without control characters");
    }
    for (const std::uint64_t size : {meta.sizeX, meta.sizeZ}) {
        if (size == 0 || size % kWorldSizeStep != 0 || size > kMaxWorldSize) {
            invalid("world size of " + std::to_string(size) +
                    " m: a positive multiple of 65,536 m is required, at most 2^36 m");
        }
    }
    for (const auto& [key, value] : meta.generation) {
        if (!isValidKey(key) || !std::isfinite(value)) {
            invalid("generation parameter \"" + key + "\": [a-z0-9_.]{1,64} and a finite value");
        }
    }
    if (!std::isfinite(meta.spawn.y) || !inWorld(meta.spawn.x, meta.sizeX) ||
        !inWorld(meta.spawn.z, meta.sizeZ)) {
        invalid("spawn point outside the world");
    }
    for (const auto& [tag, payload] : meta.unknownOptionalFields) {
        if ((tag & kOptionalFieldBit) == 0 ||
            payload.size() > std::numeric_limits<std::uint32_t>::max()) {
            invalid("field " + tagText(tag) + " is not an optional field");
        }
    }
}

std::vector<std::uint8_t> serializeWorldMeta(const WorldMeta& meta) {
    validateWorldMeta(meta);
    constexpr std::size_t kFixedFields = 160;
    ByteWriter out = storage::beginFrame(kMagic, kWorldFormatVersion,
                                         kFixedFields + meta.name.size() +
                                             meta.generation.size() * (kMaxSettingKeyBytes + 9));
    ByteWriter payload;
    payload.bytes(textBytes(meta.name));
    field(out, kName, payload);
    const auto u64Field = [&out](std::uint16_t tag, std::uint64_t value) {
        ByteWriter single;
        single.u64(value);
        field(out, tag, single);
    };
    u64Field(kSeed, meta.seed);
    payload = ByteWriter();
    payload.u64(meta.sizeX);
    payload.u64(meta.sizeZ);
    field(out, kSize, payload);
    payload = ByteWriter();
    payload.u32(meta.generatorVersion);
    field(out, kGenerator, payload);
    field(out, kGeneration, generationPayload(meta.generation));
    u64Field(kCreated, static_cast<std::uint64_t>(meta.createdAt));
    u64Field(kSaved, static_cast<std::uint64_t>(meta.savedAt));
    payload = ByteWriter();
    payload.f64(meta.spawn.x);
    payload.f64(meta.spawn.y);
    payload.f64(meta.spawn.z);
    field(out, kSpawn, payload);
    u64Field(kPlayTime, meta.playTimeSeconds);
    for (const auto& [tag, bytes] : meta.unknownOptionalFields) {
        payload = ByteWriter();
        payload.bytes(bytes);
        field(out, tag, payload);
    }
    return storage::sealFrame(std::move(out));
}

WorldMeta deserializeWorldMeta(std::span<const std::uint8_t> bytes) {
    // Version 1 is the only one; a later version reads the older ones here, the fields they
    // lack taking their defaults.
    const storage::Frame frame =
        storage::openFrame(bytes, kMagic, kWorldFormatVersion, "world.meta");
    ByteReader in(frame.body, "invalid world.meta: ");
    WorldMeta meta;
    std::uint32_t previous = 0;
    std::uint32_t seen = 0; // bit t: the required field t was read
    while (in.remaining() > 0) {
        const std::uint16_t tag = in.u16();
        const std::size_t size = in.u32();
        if (tag <= previous) {
            in.fail("field " + tagText(tag) + " out of order or repeated");
        }
        previous = tag;
        if ((tag & kOptionalFieldBit) != 0) {
            const std::span<const std::uint8_t> payload = in.take(size);
            meta.unknownOptionalFields.emplace(tag, std::vector(payload.begin(), payload.end()));
            continue;
        }
        if (tag > kPlayTime) {
            in.fail("unknown field " + tagText(tag) + ", written by a newer version of the game?");
        }
        ByteReader payload = in.sub(size, std::string("field ") + kFieldNames[tag] + ": ");
        readField(payload, tag, meta);
        payload.expectEnd("too long");
        seen |= 1U << tag;
    }
    for (std::size_t tag = kName; tag <= kPlayTime; ++tag) {
        if ((seen & (1U << tag)) == 0) {
            in.fail(std::string("missing field ") + kFieldNames[tag]);
        }
    }
    validateWorldMeta(meta);
    return meta;
}

bool generatorChanged(const WorldMeta& meta) {
    return meta.generatorVersion != kGeneratorVersion;
}

std::string generatorWarning(const WorldMeta& meta) {
    if (!generatorChanged(meta)) {
        return {};
    }
    return "world generated by generator version " + std::to_string(meta.generatorVersion) +
           ", this game runs version " + std::to_string(kGeneratorVersion) +
           ": the terrain away from the modifications may differ";
}

std::int64_t unixNow() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
    return static_cast<std::int64_t>(seconds.count());
}

} // namespace terrain
