// The world of terrain_probe (probe_world.hpp).
#include "probe_world.hpp"

#include "terrain/storage/planet_file.hpp"
#include "terrain/storage/storage_error.hpp"
#include "terrain/storage/world_meta.hpp"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <system_error>

namespace probe {

namespace {

constexpr std::size_t kTilesKept = 8;

terrain::PlanetParams planetParams(const Options& o) {
    terrain::PlanetParams params;
    params.threads = o.threads; // not part of what makes a file stale
    return params;
}

} // namespace

ProbeWorld::ProbeWorld(const Options& options, std::shared_ptr<const terrain::PlanetLevel> planet)
    : options_(options), seed_(options.seed),
      relief_(options.seed, terrain::HeightfieldParams{}, options.world),
      planet_(std::move(planet)) {
    if (planet_ != nullptr) {
        hydrology_ = std::make_shared<const terrain::Hydrology>(seed_, relief_, planet_);
    }
    std::optional<terrain::CaveParams> caves;
    if (options.caves) {
        caves = terrain::CaveParams{};
    }
    tiles_ = std::make_unique<terrain::TerrainTiles>(seed_, terrain::HeightfieldParams{},
                                                     terrain::EcologyParams{}, caves,
                                                     options.world, hydrology_);
}

const terrain::TileData& ProbeWorld::tile(const terrain::TileKey& key) const {
    for (const auto& [k, data] : cache_) {
        if (k == key) {
            return data;
        }
    }
    if (cache_.size() >= kTilesKept) {
        cache_.erase(cache_.begin());
    }
    cache_.emplace_back(key, tiles_->generate({key, 0}));
    return cache_.back().second;
}

OpenedPlanet openPlanet(const Options& o) {
    OpenedPlanet out;
    if (o.planet.empty()) {
        return out;
    }
    bool built = false;
    out.level = std::make_shared<const terrain::PlanetLevel>(terrain::openPlanetLevel(
        o.planet, o.seed, terrain::HeightfieldParams{}, planetParams(o), o.world, &built));
    out.status = built ? "built" : "read";
    return out;
}

PlanetCheck checkPlanet(const Options& o) {
    PlanetCheck check;
    std::error_code error;
    if (!std::filesystem::is_regular_file(o.planetCheck, error)) {
        check.status = "missing";
        check.reason = "no file at " + o.planetCheck.string();
        return check;
    }
    try {
        const terrain::PlanetLevel level =
            terrain::loadPlanetFile(o.planetCheck, o.seed, planetParams(o), o.world);
        check.status = "ok";
        check.checksum = level.checksum();
    } catch (const terrain::StorageError& e) {
        check.reason = e.what();
        switch (e.kind()) {
        case terrain::StorageErrorKind::Io:
            check.status = "unreadable";
            break;
        case terrain::StorageErrorKind::NewerVersion:
            check.status = "newer";
            break;
        case terrain::StorageErrorKind::Invalid:
            check.status = check.reason.find("stale") != std::string::npos ? "stale" : "damaged";
            break;
        case terrain::StorageErrorKind::Truncated:
        case terrain::StorageErrorKind::Corrupt:
            check.status = "damaged";
            break;
        }
    }
    return check;
}

std::string hex(std::uint64_t value) {
    char text[32];
    std::snprintf(text, sizeof text, "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

namespace {

Record worldOf(std::uint64_t seed, const terrain::WorldSize& world) {
    Record r;
    r.integer("seed", static_cast<std::int64_t>(seed))
        .number("x", world.x, 0, "m")
        .number("z", world.z, 0, "m")
        .integer("generatorVersion", terrain::kGeneratorVersion);
    return r;
}

} // namespace

Record checkRecord(const Options& o, const PlanetCheck& check) {
    Record r;
    r.text("tool", "terrain_probe").integer("format", 1).add("world", worldOf(o.seed, o.world));
    Record planet;
    planet.text("file", o.planetCheck.string())
        .text("status", check.status)
        .integer("fileVersion", terrain::kPlanetFileVersion);
    if (check.status == "ok") {
        planet.text("checksum", hex(check.checksum));
    } else {
        planet.none("checksum");
    }
    planet.text("reason", check.reason);
    return r.add("planetCheck", std::move(planet));
}

Record worldRecord(const ProbeWorld& w) { return worldOf(w.seed(), w.world()); }

Record planetRecord(const ProbeWorld& w, const std::string& status) {
    Record r;
    r.text("file", w.options().planet.string()).text("status", status);
    if (w.planet() != nullptr) {
        r.text("checksum", hex(w.planet()->checksum()));
    } else {
        r.none("checksum");
    }
    return r;
}

} // namespace probe
