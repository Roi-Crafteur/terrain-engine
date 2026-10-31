#include "dev_commands.hpp"

#include "terrain/dev/json_value.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace terrain::unreal {

namespace {

std::optional<double> numberOf(const std::string& text) {
    return dev::parseJsonNumber(text);
}

template <class T>
std::optional<T> refuse(std::string& error, std::string message) {
    error = std::move(message);
    return std::nullopt;
}

struct ModeName {
    ViewMode mode;
    std::string_view french;
    std::string_view english;
};

constexpr std::array<ModeName, kViewModeCount> kModes = {{
    {ViewMode::Normal, "normal", "normal"},
    {ViewMode::Wireframe, "filaire", "wireframe"},
    {ViewMode::Lod, "lod", "lod"},
    {ViewMode::Rock, "roche", "rock"},
    {ViewMode::Water, "eau", "water"},
    {ViewMode::Dry, "seche", "dry"},
    {ViewMode::Normals, "normales", "normals"},
    {ViewMode::Slope, "pente", "slope"},
}};

constexpr std::array<std::string_view, kHudPageCount> kPages = {"off", "player", "aimed",
                                                                 "perf"};

} // namespace

std::optional<EyeArgument> parseEyeArgument(const std::string& text) {
    if (!text.empty() && text.front() == '+') {
        const std::optional<double> height = numberOf(text.substr(1));
        if (!height || *height < 0.0) {
            return std::nullopt;
        }
        return EyeArgument{*height, dev::EyeMode::Ground};
    }
    const std::optional<double> altitude = numberOf(text);
    if (!altitude) {
        return std::nullopt;
    }
    return EyeArgument{*altitude, dev::EyeMode::Altitude};
}

std::optional<GotoCommand> parseGoto(std::span<const std::string> args, std::string& error) {
    constexpr const char* kUsage =
        "expected Terrain.Goto <x m> <z m> [eye: altitude m, or +height m above the ground or "
        "the water] or Terrain.Goto <place>";
    GotoCommand command;
    if (args.size() == 1) {
        if (!dev::validPlaceName(args[0])) {
            return refuse<GotoCommand>(error, kUsage);
        }
        command.place = args[0];
        return command;
    }
    if (args.size() != 2 && args.size() != 3) {
        return refuse<GotoCommand>(error, kUsage);
    }
    const std::optional<double> x = numberOf(args[0]);
    const std::optional<double> z = numberOf(args[1]);
    if (!x || !z) {
        return refuse<GotoCommand>(error, kUsage);
    }
    command.x = *x;
    command.z = *z;
    if (args.size() == 3) {
        command.eye = parseEyeArgument(args[2]);
        if (!command.eye) {
            return refuse<GotoCommand>(error, kUsage);
        }
    }
    return command;
}

std::optional<LookCommand> parseLook(std::span<const std::string> args, std::string& error) {
    constexpr const char* kUsage =
        "expected Terrain.Look <heading: degrees clockwise from the north> <pitch: degrees up, "
        "-90 to 90>";
    const std::optional<double> heading = args.size() == 2 ? numberOf(args[0]) : std::nullopt;
    const std::optional<double> pitch = args.size() == 2 ? numberOf(args[1]) : std::nullopt;
    if (!heading || !pitch || *pitch < -90.0 || *pitch > 90.0) {
        return refuse<LookCommand>(error, kUsage);
    }
    double wrapped = std::fmod(*heading, 360.0);
    wrapped = wrapped < 0.0 ? wrapped + 360.0 : wrapped;
    return LookCommand{wrapped >= 360.0 ? 0.0 : wrapped, *pitch};
}

std::optional<EyeArgument> parseEye(std::span<const std::string> args, std::string& error) {
    std::optional<EyeArgument> eye =
        args.size() == 1 ? parseEyeArgument(args[0]) : std::optional<EyeArgument>{};
    if (!eye) {
        error = "expected Terrain.Eye <altitude m> or Terrain.Eye +<height m above the ground or "
                "the water>";
    }
    return eye;
}

std::optional<SaveCommand> parseSave(std::span<const std::string> args, std::string& error) {
    if (args.empty() || !dev::validPlaceName(args[0])) {
        return refuse<SaveCommand>(
            error, "expected Terrain.Save <name: letters, digits, _ - .> [note...]");
    }
    SaveCommand command{args[0], {}};
    for (std::size_t i = 1; i < args.size(); ++i) {
        command.note += (i > 1 ? " " : "") + args[i];
    }
    return command;
}

std::optional<std::string> parseShot(std::span<const std::string> args, std::string& error) {
    if (args.size() != 1 || !dev::validPlaceName(args[0])) {
        return refuse<std::string>(error,
                                   "expected Terrain.Shot <name: letters, digits, _ - .>");
    }
    return args[0];
}

std::optional<std::vector<std::string>> parseViews(std::span<const std::string> args,
                                                   std::string& error) {
    if (args.empty()) {
        return refuse<std::vector<std::string>>(
            error, "expected Terrain.Views <place, \"*\" or \"prefix*\">...");
    }
    return std::vector<std::string>(args.begin(), args.end());
}

std::optional<SeedCommand> parseSeed(std::span<const std::string> args, std::string& error) {
    constexpr const char* kUsage = "expected Terrain.Seed <seed: a whole number> "
                                   "[surface|caves|regions]";
    if (args.empty() || args.size() > 2) {
        return refuse<SeedCommand>(error, kUsage);
    }
    const std::string& text = args[0];
    const bool negative = !text.empty() && text.front() == '-';
    const std::size_t first = negative ? 1 : 0;
    if (text.size() == first || text.size() - first > 19) {
        return refuse<SeedCommand>(error, kUsage);
    }
    std::uint64_t magnitude = 0;
    for (std::size_t i = first; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return refuse<SeedCommand>(error, kUsage);
        }
        magnitude = magnitude * 10 + static_cast<std::uint64_t>(text[i] - '0');
    }
    const auto most = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (magnitude > most + (negative ? 1U : 0U)) {
        return refuse<SeedCommand>(error, kUsage);
    }
    SeedCommand command;
    command.seed = negative ? static_cast<std::int64_t>(~magnitude + 1)
                            : static_cast<std::int64_t>(magnitude);
    if (args.size() == 2) {
        const std::array<std::pair<std::string_view, WorldPreset>, 3> presets = {{
            {"surface", WorldPreset::Surface},
            {"caves", WorldPreset::Caves},
            {"regions", WorldPreset::Regions},
        }};
        command.preset = WorldPreset::Keep;
        for (const auto& [name, preset] : presets) {
            command.preset = args[1] == name ? preset : command.preset;
        }
        if (command.preset == WorldPreset::Keep) {
            return refuse<SeedCommand>(error, kUsage);
        }
    }
    return command;
}

std::string_view presetName(WorldPreset preset) {
    constexpr std::array<std::string_view, 4> kNames = {"kept", "surface", "caves", "regions"};
    return kNames[static_cast<std::size_t>(preset)];
}

std::optional<ViewMode> parseViewMode(std::span<const std::string> args, std::string& error) {
    if (args.size() == 1) {
        for (const ModeName& name : kModes) {
            if (args[0] == name.french || args[0] == name.english) {
                return name.mode;
            }
        }
    }
    std::string usage = "expected Terrain.View";
    for (std::size_t i = 0; i < kModes.size(); ++i) {
        usage += (i == 0 ? " " : "|") + std::string(kModes[i].french);
    }
    return refuse<ViewMode>(error, usage);
}

std::string_view viewModeName(ViewMode mode) {
    return kModes[static_cast<std::size_t>(mode)].french;
}

HudPage nextHudPage(HudPage page) {
    return static_cast<HudPage>((static_cast<std::size_t>(page) + 1) % kHudPageCount);
}

std::optional<HudPage> parseHud(std::span<const std::string> args, HudPage current,
                                std::string& error) {
    if (args.empty()) {
        return nextHudPage(current);
    }
    if (args.size() == 1 && args[0].size() == 1 && args[0][0] >= '0' &&
        args[0][0] < static_cast<char>('0' + kHudPageCount)) {
        return static_cast<HudPage>(args[0][0] - '0');
    }
    return refuse<HudPage>(error,
                           "expected Terrain.Hud [0: off, 1: player, 2: aimed, 3: performance]");
}

std::string_view hudPageName(HudPage page) {
    return kPages[static_cast<std::size_t>(page)];
}

} // namespace terrain::unreal
