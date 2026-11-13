// The experimental flags of the world (experimental_flags.hpp).
#include "terrain/generation/experimental_flags.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace terrain {

namespace {

// One row per flag, sorted by name: see experimental_flags.hpp to add one.
// (Agent 46: a copy of the mechanism of agent 51, aeb5347, with the rows of agent 46 only; its
// header and its table take them when its pull request is on main.)
constexpr std::array kFlags = {
    ExperimentalFlag{"karst", "the karst: towers, cockpits, dolines, poljes (agent 46)",
                     [](WorldParams& p) { p.relief.geography.karst = true; }},
    ExperimentalFlag{"tablelands",
                     "the plateaus, mesas, canyons, high surfaces of the plateaus (agent 46)",
                     [](WorldParams& p) { p.relief.geography.tablelands = true; }},
};

bool separator(char c) {
    return c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';';
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// The names of text, as written, each once, in the order met.
std::vector<std::string> namesOf(std::string_view text) {
    std::vector<std::string> names;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && separator(text[at])) {
            ++at;
        }
        std::size_t end = at;
        while (end < text.size() && !separator(text[end])) {
            ++end;
        }
        if (end > at) {
            std::string name(text.substr(at, end - at));
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(std::move(name));
            }
        }
        at = end;
    }
    return names;
}

const ExperimentalFlag* findFlag(const std::string& written) {
    std::string name = written;
    std::transform(name.begin(), name.end(), name.begin(), lower);
    for (const ExperimentalFlag& flag : kFlags) {
        if (name == flag.name) {
            return &flag;
        }
    }
    return nullptr;
}

AppliedFlags applyTo(std::string_view text, WorldParams* params) {
    AppliedFlags out;
    std::vector<const ExperimentalFlag*> applied;
    for (const std::string& name : namesOf(text)) {
        const ExperimentalFlag* flag = findFlag(name);
        if (flag == nullptr) {
            out.unknown.push_back(name);
        } else if (std::find(applied.begin(), applied.end(), flag) == applied.end()) {
            applied.push_back(flag);
        }
    }
    // In the order of the table, sorted by name: the canonical text, whatever the order written.
    std::sort(applied.begin(), applied.end());
    for (const ExperimentalFlag* flag : applied) {
        if (params != nullptr) {
            flag->apply(*params);
        }
        out.text += (out.text.empty() ? "" : ",") + std::string(flag->name);
    }
    return out;
}

} // namespace

std::span<const ExperimentalFlag> experimentalFlags() {
    return kFlags;
}

AppliedFlags applyExperimentalFlags(std::string_view text, WorldParams& params) {
    return applyTo(text, &params);
}

std::string canonicalFlags(std::string_view text) {
    return applyTo(text, nullptr).text;
}

std::string knownFlagNames() {
    std::string names;
    for (const ExperimentalFlag& flag : kFlags) {
        names += (names.empty() ? "" : ", ") + std::string(flag.name);
    }
    return names;
}

} // namespace terrain
