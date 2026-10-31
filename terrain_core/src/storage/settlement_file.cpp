// What a world keeps of its settlement (settlement_file.hpp).
#include "terrain/storage/settlement_file.hpp"

#include "file_commit.hpp"
#include "terrain/storage/storage_error.hpp"

#include <array>
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace terrain {

namespace {

std::string hex64(std::uint64_t v) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%016" PRIx64, v);
    return text;
}

// A share of [0, 1] to the nearest millionth: a number with a short decimal, written exactly.
double millionths(double share) {
    return static_cast<double>(static_cast<std::int64_t>(share * 1e6 + 0.5)) / 1e6;
}

// The reading of a document: every refusal a StorageError naming the file, the member and why.
class Reader {
public:
    explicit Reader(std::string source) : source_(std::move(source)) {}

    [[noreturn]] void fail(StorageErrorKind kind, const std::string& at,
                           const std::string& why) const {
        throw StorageError(kind, source_ + ": " + at + ": " + why);
    }

    // The object at, holding only keys, all of them but those of optional.
    void only(const json::Value& v, const std::string& at,
              std::initializer_list<std::string_view> keys,
              std::initializer_list<std::string_view> optional = {}) const {
        if (!v.isObject()) {
            fail(StorageErrorKind::Invalid, at, "not an object");
        }
        for (const std::string& key : v.keys()) {
            if (!contains(keys, key) && !contains(optional, key)) {
                fail(StorageErrorKind::Invalid, at, "unknown member '" + key + "'");
            }
        }
        for (const std::string_view key : keys) {
            if (v.find(key) == nullptr) {
                fail(StorageErrorKind::Invalid, at, "missing member '" + std::string(key) + "'");
            }
        }
    }

    [[nodiscard]] std::string text(const json::Value& object, std::string_view key,
                                   const std::string& at) const {
        const json::Value& v = *object.find(key);
        if (!v.isString()) {
            fail(StorageErrorKind::Invalid, at + "." + std::string(key), "not a string");
        }
        return v.asString();
    }

    [[nodiscard]] double number(const json::Value& object, std::string_view key,
                                const std::string& at) const {
        const json::Value& v = *object.find(key);
        if (!v.isNumber()) {
            fail(StorageErrorKind::Invalid, at + "." + std::string(key), "not a number");
        }
        return v.asNumber();
    }

    [[nodiscard]] int integer(const json::Value& object, std::string_view key,
                              const std::string& at) const {
        const double v = number(object, key, at);
        if (!(v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) ||
            static_cast<double>(static_cast<int>(v)) != v) {
            fail(StorageErrorKind::Invalid, at + "." + std::string(key), "not an integer");
        }
        return static_cast<int>(v);
    }

    // "0x" and 16 hex digits.
    [[nodiscard]] std::uint64_t hex(const json::Value& object, std::string_view key,
                                    const std::string& at) const {
        const std::string s = text(object, key, at);
        std::uint64_t v = 0;
        bool valid = s.size() == 18 && s[0] == '0' && s[1] == 'x';
        for (std::size_t k = 2; valid && k < s.size(); ++k) {
            const char c = s[k];
            const bool digit = c >= '0' && c <= '9';
            const bool letter = c >= 'a' && c <= 'f';
            valid = digit || letter;
            v = v * 16u + static_cast<std::uint64_t>(digit ? c - '0' : c - 'a' + 10);
        }
        if (!valid) {
            fail(StorageErrorKind::Invalid, at + "." + std::string(key),
                 "not \"0x\" and 16 lowercase hex digits");
        }
        return v;
    }

private:
    static bool contains(std::initializer_list<std::string_view> keys, std::string_view key) {
        for (const std::string_view k : keys) {
            if (k == key) {
                return true;
            }
        }
        return false;
    }

    std::string source_;
};

json::Value forcedOf(const settlement::SettlementRequest& r) {
    json::Value forced = json::Value::object();
    if (r.civilizedShare) {
        forced.set("civilizedShare", json::Value::number(*r.civilizedShare));
    }
    if (r.majorKingdoms) {
        forced.set("majorKingdoms", json::Value::number(*r.majorKingdoms));
    }
    if (r.minorStates) {
        forced.set("minorStates", json::Value::number(*r.minorStates));
    }
    return forced;
}

void readForced(const Reader& in, const json::Value& f, settlement::SettlementRequest& r) {
    const std::string at = "world.forced";
    in.only(f, at, {}, {"civilizedShare", "majorKingdoms", "minorStates"});
    if (f.find("civilizedShare") != nullptr) {
        r.civilizedShare = in.number(f, "civilizedShare", at);
    }
    if (f.find("majorKingdoms") != nullptr) {
        r.majorKingdoms = in.integer(f, "majorKingdoms", at);
    }
    if (f.find("minorStates") != nullptr) {
        r.minorStates = in.integer(f, "minorStates", at);
    }
}

void readGenerationState(const Reader& in, const json::Value& g, SettlementRecord& r) {
    const std::string at = "world.generationState";
    if (!g.isObject()) {
        in.fail(StorageErrorKind::Invalid, at, "not an object");
    }
    for (std::size_t k = 0; k < g.size(); ++k) {
        const std::string state = in.text(g, g.keys()[k], at);
        if (state != "done" && state != "pending") {
            in.fail(StorageErrorKind::Invalid, at + "." + g.keys()[k],
                    "\"" + state + "\" is neither \"done\" nor \"pending\"");
        }
        r.generationState.emplace_back(g.keys()[k], state);
    }
}

} // namespace

std::uint64_t settlementConfigHash(const json::Value& config) {
    return json::fnv1a64(json::write(config, false));
}

json::Value settlementDocument(const SettlementRecord& record) {
    json::Value resolved = json::Value::object();
    const double share = record.resolved.civilizedShare;
    resolved.set("civilizedShare", json::Value::number(millionths(share)));
    resolved.set("civilizedShareBits",
                 json::Value::string(hex64(std::bit_cast<std::uint64_t>(share))));
    resolved.set("resolvedMajorKingdoms", json::Value::number(record.resolved.majorKingdoms));
    resolved.set("resolvedMinorStates", json::Value::number(record.resolved.minorStates));
    json::Value state = json::Value::object();
    for (const auto& [stage, value] : record.generationState) {
        state.set(stage, json::Value::string(value));
    }
    json::Value world = json::Value::object();
    world.set("seed", json::Value::string(hex64(record.worldSeed)));
    world.set("secretSeedId", json::Value::string(record.secretSeedId));
    world.set("worldPreset", json::Value::string(record.resolved.preset));
    world.set("forced", forcedOf(record.request));
    world.set("resolved", std::move(resolved));
    world.set("configVersion", json::Value::string(record.configVersion));
    world.set("config", record.config);
    world.set("configHash", json::Value::string(hex64(settlementConfigHash(record.config))));
    world.set("kingdomsChecksum", json::Value::string(hex64(record.kingdomsChecksum)));
    world.set("generationState", std::move(state));
    json::Value document = json::Value::object();
    document.set("format", json::Value::number(kSettlementFileVersion));
    document.set("world", std::move(world));
    return document;
}

void saveSettlementFile(const std::filesystem::path& file, const SettlementRecord& record) {
    std::string text;
    try {
        text = json::write(settlementDocument(record)) + "\n";
    } catch (const json::Error& e) {
        throw StorageError(StorageErrorKind::Invalid,
                           storage::pathText(file) + ": not written: " + e.what());
    }
    const std::array<storage::FileWrite, 1> writes = {
        storage::FileWrite{file, std::vector<std::uint8_t>(text.begin(), text.end())}};
    storage::commitFiles(writes, {}, {.keepBackups = false, .createDirectories = true});
}

SettlementRecord loadSettlementFile(const std::filesystem::path& file) {
    const std::vector<std::uint8_t> bytes = storage::readFileBytes(file);
    const std::string source = storage::pathText(file);
    const Reader in(source);
    json::Value document;
    try {
        document = json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                                bytes.size()),
                               source);
    } catch (const json::Error& e) {
        throw StorageError(StorageErrorKind::Invalid, e.what());
    }
    in.only(document, "document", {"format", "world"});
    const int format = in.integer(document, "format", "document");
    if (format > kSettlementFileVersion) {
        in.fail(StorageErrorKind::NewerVersion, "format",
                "format " + std::to_string(format) + " is newer than this build (" +
                    std::to_string(kSettlementFileVersion) + ")");
    }
    if (format < 1) {
        in.fail(StorageErrorKind::Invalid, "format", "unknown format " + std::to_string(format));
    }
    const json::Value& w = *document.find("world");
    in.only(w, "world",
            {"seed", "secretSeedId", "worldPreset", "forced", "resolved", "configVersion",
             "config", "configHash", "kingdomsChecksum", "generationState"});
    SettlementRecord r;
    r.worldSeed = in.hex(w, "seed", "world");
    r.secretSeedId = in.text(w, "secretSeedId", "world");
    r.resolved.preset = in.text(w, "worldPreset", "world");
    r.request.preset = r.resolved.preset;
    readForced(in, *w.find("forced"), r.request);
    const json::Value& resolved = *w.find("resolved");
    in.only(resolved, "world.resolved",
            {"civilizedShare", "civilizedShareBits", "resolvedMajorKingdoms",
             "resolvedMinorStates"});
    r.resolved.civilizedShare =
        std::bit_cast<double>(in.hex(resolved, "civilizedShareBits", "world.resolved"));
    if (in.number(resolved, "civilizedShare", "world.resolved") !=
        millionths(r.resolved.civilizedShare)) {
        in.fail(StorageErrorKind::Invalid, "world.resolved.civilizedShare",
                "does not match civilizedShareBits");
    }
    r.resolved.majorKingdoms = in.integer(resolved, "resolvedMajorKingdoms", "world.resolved");
    r.resolved.minorStates = in.integer(resolved, "resolvedMinorStates", "world.resolved");
    r.configVersion = in.text(w, "configVersion", "world");
    r.config = *w.find("config");
    const std::uint64_t hash = in.hex(w, "configHash", "world");
    if (settlementConfigHash(r.config) != hash) {
        in.fail(StorageErrorKind::Corrupt, "world.config",
                "the copy of the configuration does not match its configHash " + hex64(hash) +
                    " (" + hex64(settlementConfigHash(r.config)) +
                    " computed): the file was altered");
    }
    const json::Value* copied = r.config.find("world");
    const json::Value* version = copied != nullptr ? copied->find("configVersion") : nullptr;
    if (version == nullptr || !version->isString() || version->asString() != r.configVersion) {
        in.fail(StorageErrorKind::Invalid, "world.configVersion",
                "\"" + r.configVersion + "\" is not the configVersion of the copy");
    }
    r.kingdomsChecksum = in.hex(w, "kingdomsChecksum", "world");
    readGenerationState(in, *w.find("generationState"), r);
    return r;
}

} // namespace terrain
