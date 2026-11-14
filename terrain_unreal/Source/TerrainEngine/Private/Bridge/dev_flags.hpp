#pragma once

// Terrain.Flags of the tools of the developers (agent 51): the experimental flags of the next Plays
// of this session of the editor, over ATerrainWorld::Experimental, the map never changed. Kept by
// the process (an editor started again forgets them); set only by the console command, which
// exists only with TERRAIN_DEV_TOOLS (never in Shipping). A world of such flags reads its planet
// file aside, under Saved/TerrainEngine/dev (kDevFlagsPlanetPath), never the planet file of the
// user: built there if missing, refused if it holds other flags.

#include "planet_job.hpp"
#include "session_flags.hpp"
#include "terrain_session.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace terrain::unreal {

// The planet file of a world of the flags of Terrain.Flags: "{Flags}" is their canonical text,
// "+" between the names (planet_20261019_seas+volcanoes.bin).
inline constexpr const char* kDevFlagsPlanetPath =
    "Saved/TerrainEngine/dev/planet_{Seed}_{Flags}.bin";

// Terrain.Flags <text|none|reset>: set the flags ("none" alone: no flag), give the next Plays
// back to the setting of the actor ("reset" alone), or, without argument, tell them. The words are
// joined by commas; "none" and "reset" are read whatever their case (no flag takes these names,
// experimental_flags.hpp).
struct DevFlagsCommand {
    bool set = false;
    bool reset = false;
    std::string text;

    friend bool operator==(const DevFlagsCommand&, const DevFlagsCommand&) = default;
};
[[nodiscard]] DevFlagsCommand parseDevFlags(std::span<const std::string> args);

// Does what command asks: sets the flags, or forgets them (reset); nothing to tell them.
void runDevFlags(const DevFlagsCommand& command);

// The flags of Terrain.Flags, or none set.
void setDevFlags(std::string text);
[[nodiscard]] std::optional<std::string> devFlags();
void clearDevFlags(); // for the tests

// The planet path of kDevFlagsPlanetPath for the canonical text of flags (not empty).
[[nodiscard]] std::string devFlagsPlanetPath(const std::string& canonical);

// The flags of Terrain.Flags laid over params (those of the actor): its experimental text and, for
// flags of a non-empty canonical text with a planet file configured, the planet file of
// kDevFlagsPlanetPath for its seed (resolved from base, the folder of the project). Returns
// "Terrain.Flags" when it laid them, "" when none are set (params untouched).
std::string applyDevFlags(SessionParams& params, const std::string& base,
                          std::span<const ExperimentalFlag> table = experimentalFlags());

// The same for the planet job of Terrain.Seed: its text of flags (that of the actor, or of
// Terrain.Flags), and its choice (a file of the dev folder, written there if need be).
void applyDevFlags(PlanetChoice& choice, std::string& experimental, std::uint64_t seed,
                   const std::string& base,
                   std::span<const ExperimentalFlag> table = experimentalFlags());

// What Terrain.Flags tells: "Terrain.Flags: \"seas\" for the next Plays of this editor, over the
// setting of the actor (planet file: Saved/TerrainEngine/dev/planet_{Seed}_seas.bin)", "...: none
// for the next Plays ...", or "Terrain.Flags: not set: the next Plays take the setting of the
// actor"; then a warning per unknown name (flagLines).
[[nodiscard]] std::vector<FlagLine> devFlagsLines(
    std::span<const ExperimentalFlag> table = experimentalFlags());

} // namespace terrain::unreal
