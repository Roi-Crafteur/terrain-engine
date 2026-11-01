#pragma once

#include "terrain/generation/settlement/world_preset.hpp"
#include "terrain/json/json.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// What a world keeps of its settlement (docs/PEUPLEMENT.md, section 6 bis, "copie complète de la
// configuration"): the file settlement.json of the folder of the world, in canonical JSON. It
// holds the seed, the id of the secret seed (empty while there is none), the preset asked and the
// values forced, the values used, the version of the configuration, a whole copy of the data of
// the settlement (SettlementData::document: world.json, the resources and the peoples) and its
// fingerprint configHash, the checksum of the kingdoms generated and the state of each stage of
// the generation. The copy keeps the world reproducible when the global data files change; the
// fingerprint tells a copy that was altered, which is refused when read. A number that does not
// fit a JSON number exactly (the seed, the fingerprints) is written as "0x" and 16 hex digits;
// the civilized share drawn, both to the nearest millionth (to read) and by its bits (exact).
//
// {"format": 1,
//  "world": {"seed": "0x...", "secretSeedId": "", "worldPreset": "wild",
//            "forced": {"majorKingdoms": 12},
//            "resolved": {"civilizedShare": 0.153452, "civilizedShareBits": "0x...",
//                         "resolvedMajorKingdoms": 12, "resolvedMinorStates": 85},
//            "configVersion": "1.0", "config": {...}, "configHash": "0x...",
//            "kingdomsChecksum": "0x...",
//            "generationState": {"terrain": "done", "rivers": "done", ...}}}
namespace terrain {

inline constexpr const char* kSettlementFileName = "settlement.json";
inline constexpr int kSettlementFileVersion = 1;

struct SettlementRecord {
    std::uint64_t worldSeed = 0;
    std::string secretSeedId;                    // empty: no secret seed yet
    settlement::SettlementRequest request;       // the preset asked (empty: the default), forced
    settlement::ResolvedPreset resolved;         // the values used
    std::string configVersion;                   // SettlementConfig::version, of the copy
    json::Value config;                          // SettlementData::document()
    std::uint64_t kingdomsChecksum = 0;          // Kingdoms::checksum
    // Each stage of the generation and its state, "done" or "pending", in order.
    std::vector<std::pair<std::string, std::string>> generationState;
};

// The fingerprint of a copy of the configuration: FNV-1a 64 of its compact canonical text, the
// same as SettlementData::fingerprint of the data it holds.
[[nodiscard]] std::uint64_t settlementConfigHash(const json::Value& config);

// The canonical document of a record, its configHash computed from its copy.
[[nodiscard]] json::Value settlementDocument(const SettlementRecord& record);

// Writes record to file, whole or not at all. Throws StorageError (Io).
void saveSettlementFile(const std::filesystem::path& file, const SettlementRecord& record);

// Reads back the record of file. Throws StorageError: Io if it cannot be read; Invalid if it is
// not such a file (the message says which member and why, a configVersion other than the one of
// its copy included); NewerVersion if its format is newer;
// Corrupt if its copy of the configuration does not match its configHash (an altered file). The
// data of the copy are read with settlement::settlementDataOf(record.config, ...).
[[nodiscard]] SettlementRecord loadSettlementFile(const std::filesystem::path& file);

} // namespace terrain
