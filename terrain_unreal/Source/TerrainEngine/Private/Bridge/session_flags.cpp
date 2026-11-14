#include "session_flags.hpp"

namespace terrain::unreal {

SessionFlags sessionFlags(const std::string& text, std::span<const ExperimentalFlag> table) {
    SessionFlags flags;
    flags.applied = applyExperimentalFlags(text, flags.params, table);
    return flags;
}

std::vector<FlagLine> flagLines(const AppliedFlags& applied,
                                std::span<const ExperimentalFlag> table, const std::string& from) {
    std::vector<FlagLine> lines;
    lines.push_back({false, "Terrain World: experimental flags: " +
                                (applied.text.empty() ? std::string("none")
                                                      : "\"" + applied.text + "\"") +
                                (from.empty() ? std::string() : " (from " + from + ")")});
    const std::string known = knownFlagNames(table);
    for (const std::string& name : applied.unknown) {
        lines.push_back({true, "Terrain World: unknown experimental flag '" + name + "' (" +
                                   (known.empty() ? "no flag known yet" : "known: " + known) +
                                   ")"});
    }
    return lines;
}

} // namespace terrain::unreal
