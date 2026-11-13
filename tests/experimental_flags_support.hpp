#pragma once

// PROVISIONAL (agent 45), tests only: the experimental flags of agent 51 exactly as its commit
// aeb5347 has them (claude/lucid-brown-7bm89z: include/terrain/generation/experimental_flags.hpp
// and src/generation/experimental_flags.cpp, not on main yet), the same names, types, signatures
// and reading of the text, inline here, so that the variants of the goldens (golden_support.hpp)
// are written against them. Its table holds the row of agent 45 only ("seas" is agent 51's).
// Replaced by the header of main, the row "volcanoes" moving into its table, as soon as the small
// PR of agent 51 is merged, and before agent 45 asks for its merge.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace terrain {

// Every parameter a flag may set: the relief, the planet level, the water (the seas with it).
struct WorldParams {
    HeightfieldParams relief;
    PlanetParams planet;
    HydrologyParams water;
};

struct ExperimentalFlag {
    const char* name; // lower case letters, digits, '-' and '_'
    const char* what; // a few words, for the usage of the tools and the log
    void (*apply)(WorldParams& params);
};

struct AppliedFlags {
    // The names applied, sorted, each once, joined by commas: the canonical text ("" for none),
    // the one written in the planet files.
    std::string text;
    // The names not in the table, as written, each once, in the order met: never ignored in
    // silence (the tools stop, Unreal warns in the log).
    std::vector<std::string> unknown;
};

namespace experimental_detail {

// One row per flag, sorted by name.
inline constexpr std::array kFlags = {
    ExperimentalFlag{"volcanoes", "the volcanoes (agent 45)",
                     [](WorldParams& p) { p.relief.volcanoes.enabled = true; }},
};

inline bool separator(char c) {
    return c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';';
}

inline char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// The names of text, as written, each once, in the order met.
inline std::vector<std::string> namesOf(std::string_view text) {
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

inline const ExperimentalFlag* findFlag(const std::string& written) {
    std::string name = written;
    std::transform(name.begin(), name.end(), name.begin(), lower);
    for (const ExperimentalFlag& flag : kFlags) {
        if (name == flag.name) {
            return &flag;
        }
    }
    return nullptr;
}

inline AppliedFlags applyTo(std::string_view text, WorldParams* params) {
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

} // namespace experimental_detail

// The table of the flags, sorted by name.
[[nodiscard]] inline std::span<const ExperimentalFlag> experimentalFlags() {
    return experimental_detail::kFlags;
}

// Applies to params the flags of text: names apart by commas or white space, their case ignored,
// empty ones skipped.
inline AppliedFlags applyExperimentalFlags(std::string_view text, WorldParams& params) {
    return experimental_detail::applyTo(text, &params);
}

// The canonical text of the known names of text (AppliedFlags::text), params untouched.
[[nodiscard]] inline std::string canonicalFlags(std::string_view text) {
    return experimental_detail::applyTo(text, nullptr).text;
}

// The names of the table, for the usage of the tools.
[[nodiscard]] inline std::string knownFlagNames() {
    std::string names;
    for (const ExperimentalFlag& flag : experimental_detail::kFlags) {
        names += (names.empty() ? "" : ", ") + std::string(flag.name);
    }
    return names;
}

} // namespace terrain
