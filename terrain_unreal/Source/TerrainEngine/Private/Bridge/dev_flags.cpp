#include "dev_flags.hpp"

#include <mutex>
#include <utility>

namespace terrain::unreal {

namespace {

std::mutex& flagsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<std::string>& flagsSet() {
    static std::optional<std::string> flags;
    return flags;
}

std::string lowered(std::string text) {
    for (char& c : text) {
        c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return text;
}

std::string replaced(std::string text, char from, char to) {
    for (char& c : text) {
        c = c == from ? to : c;
    }
    return text;
}

} // namespace

DevFlagsCommand parseDevFlags(std::span<const std::string> args) {
    DevFlagsCommand command;
    if (args.empty()) {
        return command;
    }
    const std::string word = args.size() == 1 ? lowered(args[0]) : std::string();
    if (word == "reset") {
        command.reset = true;
        return command;
    }
    command.set = true;
    if (word == "none") {
        return command;
    }
    for (const std::string& name : args) {
        command.text += (command.text.empty() ? "" : ",") + name;
    }
    return command;
}

void runDevFlags(const DevFlagsCommand& command) {
    if (command.reset) {
        clearDevFlags();
    } else if (command.set) {
        setDevFlags(command.text);
    }
}

void setDevFlags(std::string text) {
    const std::lock_guard lock(flagsMutex());
    flagsSet() = std::move(text);
}

std::optional<std::string> devFlags() {
    const std::lock_guard lock(flagsMutex());
    return flagsSet();
}

void clearDevFlags() {
    const std::lock_guard lock(flagsMutex());
    flagsSet().reset();
}

std::string devFlagsPlanetPath(const std::string& canonical) {
    std::string path = kDevFlagsPlanetPath;
    const std::string token = "{Flags}";
    path.replace(path.find(token), token.size(), replaced(canonical, ',', '+'));
    return path;
}

std::string applyDevFlags(SessionParams& params, const std::string& base,
                          std::span<const ExperimentalFlag> table) {
    const std::optional<std::string> flags = devFlags();
    if (!flags) {
        return {};
    }
    params.experimental = *flags;
    const std::string canonical = canonicalFlags(*flags, table);
    if (!canonical.empty() && !params.planetFile.empty()) {
        params.planetFile = resolvePlanetPath(devFlagsPlanetPath(canonical), params.seed, base);
    }
    return "Terrain.Flags";
}

void applyDevFlags(PlanetChoice& choice, std::string& experimental, std::uint64_t seed,
                   const std::string& base, std::span<const ExperimentalFlag> table) {
    const std::optional<std::string> flags = devFlags();
    if (!flags) {
        return;
    }
    experimental = *flags;
    const std::string canonical = canonicalFlags(*flags, table);
    if (!canonical.empty() && !choice.file.empty()) {
        choice = {resolvePlanetPath(devFlagsPlanetPath(canonical), seed, base), true, {}};
    }
}

std::vector<FlagLine> devFlagsLines(std::span<const ExperimentalFlag> table) {
    const std::optional<std::string> flags = devFlags();
    if (!flags) {
        return {{false, "Terrain.Flags: not set: the next Plays take the setting of the actor"}};
    }
    const AppliedFlags applied = sessionFlags(*flags, table).applied;
    std::string line = "Terrain.Flags: " +
                       (applied.text.empty() ? std::string("none") : "\"" + applied.text + "\"") +
                       " for the next Plays of this editor, over the setting of the actor";
    if (!applied.text.empty()) {
        line += " (planet file: " + devFlagsPlanetPath(applied.text) + ")";
    }
    std::vector<FlagLine> lines = flagLines(applied, table);
    lines.front() = {false, line};
    return lines;
}

} // namespace terrain::unreal
