#include "terrain/generation/settlement/settlement_data.hpp"

#include "data_fields.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace terrain::settlement {

namespace {

// The JSON files of a folder, sorted by name (none if the folder does not exist).
std::vector<std::filesystem::path> jsonFiles(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    if (error) {
        throw json::Error(folder.generic_string() + ": cannot be listed");
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.filename().generic_string() < b.filename().generic_string();
    });
    return files;
}

// Refuses two resources of the same id.
void checkUnique(const std::vector<ResourceDef>& resources,
                 const std::vector<std::string>& sources) {
    for (std::size_t i = 0; i < resources.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (resources[i].id == resources[j].id) {
                throw json::Error(sources[i] + ": id: duplicate resource '" + resources[i].id +
                                  "' (also in " + sources[j] + ")");
            }
        }
    }
}

} // namespace

json::Value SettlementData::document() const {
    json::Value d = json::Value::object();
    d.set("world", config.document);
    json::Value list = json::Value::array();
    for (const ResourceDef& r : resources) {
        list.push(r.document);
    }
    d.set("resources", std::move(list));
    return d;
}

std::uint64_t SettlementData::fingerprint() const {
    return json::fnv1a64(json::write(document(), false));
}

const ResourceDef* SettlementData::resource(std::string_view id) const {
    for (const ResourceDef& r : resources) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

SettlementData loadSettlementData(const std::filesystem::path& folder) {
    SettlementData data;
    const std::filesystem::path world = folder / std::string(kWorldConfigFile);
    data.config = readSettlementConfig(json::parseFile(world), world.generic_string());
    std::vector<std::string> sources;
    for (const std::filesystem::path& file : jsonFiles(folder / std::string(kResourcesFolder))) {
        data.resources.push_back(readResourceDef(json::parseFile(file), file.generic_string()));
        sources.push_back(file.generic_string());
    }
    checkUnique(data.resources, sources);
    return data;
}

SettlementData settlementDataOf(const json::Value& copy, std::string_view source) {
    const Fields f(copy, std::string(source), "");
    f.only({"world", "resources"});
    SettlementData data;
    data.config = readSettlementConfig(f.member("world"), std::string(source) + ": world");
    const json::Value& list = f.member("resources");
    if (!list.isArray()) {
        f.fail(list, "resources", "expected an array");
    }
    std::vector<std::string> sources;
    for (std::size_t k = 0; k < list.size(); ++k) {
        sources.push_back(std::string(source) + ": resources[" + std::to_string(k) + "]");
        data.resources.push_back(readResourceDef(list.items()[k], sources.back()));
    }
    checkUnique(data.resources, sources);
    return data;
}

} // namespace terrain::settlement
