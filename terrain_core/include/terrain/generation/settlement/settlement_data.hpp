#pragma once

#include "terrain/generation/settlement/people.hpp"
#include "terrain/generation/settlement/resource_def.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/json/json.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

// The data files of the settlement of a world (docs/PEUPLEMENT.md): a folder holding world.json
// (SettlementConfig), resources/*.json (one resource per file) and peoples/*.json (one people per
// file), read and validated whole. A file that is not valid is refused with a clear message (json::Error: the file, the path in it,
// the reason, the line and the column).
namespace terrain::settlement {

inline constexpr std::string_view kWorldConfigFile = "world.json";
inline constexpr std::string_view kResourcesFolder = "resources";
inline constexpr std::string_view kPeoplesFolder = "peoples";

struct SettlementData {
    SettlementConfig config;
    std::vector<ResourceDef> resources; // in the order of their file names
    std::vector<People> peoples;        // in the order of their file names

    // The whole data as one document, the copy a world keeps (section 6 bis):
    // {"world": ..., "resources": [...], "peoples": [...]} in that order.
    [[nodiscard]] json::Value document() const;
    // The fingerprint of the data: FNV-1a 64 of the compact canonical text of document().
    [[nodiscard]] std::uint64_t fingerprint() const;
    // The resource of that id, or nullptr.
    [[nodiscard]] const ResourceDef* resource(std::string_view id) const;
    // The index of the people of that id in peoples, or -1.
    [[nodiscard]] int people(std::string_view id) const;
};

// The data of a folder. The files of resources/ and of peoples/ are read in the order of their
// names, so the same folder always gives the same data. Throws json::Error.
[[nodiscard]] SettlementData loadSettlementData(const std::filesystem::path& folder);

// The data of a copy (SettlementData::document) kept with a world. Throws json::Error.
[[nodiscard]] SettlementData settlementDataOf(const json::Value& copy, std::string_view source);

} // namespace terrain::settlement
