#pragma once

#include "terrain/dev/places_file.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace terrain::unreal {

// The arguments of the console commands of the tools of the developers (agent 36,
// TerrainDevCommands.cpp), parsed here to be tested without Unreal. Each parser gives its command,
// or nullopt with the reason in error (the usage of the command). Numbers are JSON numbers, the
// whole argument ("182.03", "-6185104"; dev::parseJsonNumber), whatever the locale.

// An eye: an altitude of the core ("182.03") or a height above the top of its column ("+2").
struct EyeArgument {
    double value = 0.0;
    dev::EyeMode mode = dev::EyeMode::Altitude;

    friend bool operator==(const EyeArgument&, const EyeArgument&) = default;
};
[[nodiscard]] std::optional<EyeArgument> parseEyeArgument(const std::string& text);

// Terrain.Goto x z [eye] | Terrain.Goto <place>.
struct GotoCommand {
    std::string place; // the name of a place, or empty for x and z
    double x = 0.0;
    double z = 0.0;
    std::optional<EyeArgument> eye;
};
[[nodiscard]] std::optional<GotoCommand> parseGoto(std::span<const std::string> args,
                                                   std::string& error);

// Terrain.Look heading pitch: degrees, the heading brought into [0, 360), the pitch in [-90, 90].
struct LookCommand {
    double heading = 0.0;
    double pitch = 0.0;
};
[[nodiscard]] std::optional<LookCommand> parseLook(std::span<const std::string> args,
                                                   std::string& error);

// Terrain.Eye altitude|+height.
[[nodiscard]] std::optional<EyeArgument> parseEye(std::span<const std::string> args,
                                                  std::string& error);

// Terrain.Save <name> [note...]: the note is the other arguments joined by spaces.
struct SaveCommand {
    std::string name;
    std::string note;
};
[[nodiscard]] std::optional<SaveCommand> parseSave(std::span<const std::string> args,
                                                   std::string& error);

// Terrain.Shot <name>: a valid name of a place (it names the file).
[[nodiscard]] std::optional<std::string> parseShot(std::span<const std::string> args,
                                                   std::string& error);

// Terrain.Views <names, "*" or "prefix*">...: the patterns of dev::matchPlaces.
[[nodiscard]] std::optional<std::vector<std::string>> parseViews(
    std::span<const std::string> args, std::string& error);

// Terrain.Seed <n> [surface|caves|regions]: the seed of the world and its preset, the options
// of the world of the plugin (Keep: those of the Terrain World).
enum class WorldPreset : std::uint8_t { Keep, Surface, Caves, Regions };
struct SeedCommand {
    std::int64_t seed = 0;
    WorldPreset preset = WorldPreset::Keep;
};
[[nodiscard]] std::optional<SeedCommand> parseSeed(std::span<const std::string> args,
                                                   std::string& error);
[[nodiscard]] std::string_view presetName(WorldPreset preset);

// Terrain.View <mode>: what the tiles and the horizon show (TerrainDevLooks.cpp). The French
// names of the brief and their English aliases.
enum class ViewMode : std::uint8_t { Normal, Wireframe, Lod, Rock, Water, Dry, Normals, Slope };
inline constexpr std::size_t kViewModeCount = 8;
[[nodiscard]] std::optional<ViewMode> parseViewMode(std::span<const std::string> args,
                                                    std::string& error);
[[nodiscard]] std::string_view viewModeName(ViewMode mode);

// The pages of the screen of F3: nothing, the player, the point aimed at, the performance.
enum class HudPage : std::uint8_t { Off, Player, Aimed, Perf };
inline constexpr std::size_t kHudPageCount = 4;
[[nodiscard]] HudPage nextHudPage(HudPage page);
// Terrain.Hud [0|1|2|3]: without an argument, the next page.
[[nodiscard]] std::optional<HudPage> parseHud(std::span<const std::string> args, HudPage current,
                                              std::string& error);
[[nodiscard]] std::string_view hudPageName(HudPage page);

} // namespace terrain::unreal
