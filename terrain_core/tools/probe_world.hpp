#pragma once

// The world terrain_probe samples (probe_sections.hpp), built once: the relief alone, the planet
// level and its water when there is one, the tiles a client streams over them (their heightfield
// is the ground of the Unreal frame: TerrainSession::frameAt) and their ecology.

#include "probe_options.hpp"
#include "probe_record.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace probe {

class ProbeWorld {
public:
    // The world of the options: their seed, world and caves; planet null for the relief and the
    // sea alone.
    ProbeWorld(const Options& options, std::shared_ptr<const terrain::PlanetLevel> planet);

    [[nodiscard]] std::uint64_t seed() const { return seed_; }
    [[nodiscard]] const terrain::WorldSize& world() const { return relief_.world(); }
    // The relief alone, before the valleys of the rivers.
    [[nodiscard]] const terrain::Heightfield& relief() const { return relief_; }
    // The ground of the world: the relief with its water (the heightfield of the tiles).
    [[nodiscard]] const terrain::Heightfield& field() const { return tiles_->heightfield(); }
    [[nodiscard]] const terrain::TerrainTiles& tiles() const { return *tiles_; }
    [[nodiscard]] const terrain::PlanetLevel* planet() const { return planet_.get(); }
    [[nodiscard]] const terrain::Hydrology* hydrology() const { return hydrology_.get(); }
    [[nodiscard]] const Options& options() const { return options_; }

    // The tile of this canonical key (no seam), generated once and kept among the last few:
    // the sections of one point, and the points of a line within a tile, share it. One thread.
    [[nodiscard]] const terrain::TileData& tile(const terrain::TileKey& key) const;

private:
    Options options_;
    std::uint64_t seed_;
    terrain::Heightfield relief_;
    std::shared_ptr<const terrain::PlanetLevel> planet_;
    std::shared_ptr<const terrain::Hydrology> hydrology_;
    std::unique_ptr<terrain::TerrainTiles> tiles_;
    mutable std::vector<std::pair<terrain::TileKey, terrain::TileData>> cache_;
};

// The planet level of --planet: none without it ("none"), else read from the file ("read") or
// built and written to it when it does not hold the level of the options ("built").
struct OpenedPlanet {
    std::shared_ptr<const terrain::PlanetLevel> level;
    std::string status = "none";
};
[[nodiscard]] OpenedPlanet openPlanet(const Options& options);

// --planet-check: the file read without ever being written or built. status "ok", "missing",
// "stale" (another seed, world, generator version or parameters), "damaged", "newer" or
// "unreadable", the message of the storage, and the checksum of the level when ok.
struct PlanetCheck {
    std::string status;
    std::string reason;
    std::uint64_t checksum = 0;
};
[[nodiscard]] PlanetCheck checkPlanet(const Options& options);

// "0x" and 16 hexadecimal digits.
[[nodiscard]] std::string hex(std::uint64_t value);

// The record of a check, and of the world and the planet of a probe.
[[nodiscard]] Record checkRecord(const Options& options, const PlanetCheck& check);
[[nodiscard]] Record worldRecord(const ProbeWorld& world);
[[nodiscard]] Record planetRecord(const ProbeWorld& world, const std::string& status);

} // namespace probe
