#pragma once

// PROVISIONAL (agent 45), tests only: the experimental flags of agent 51 as its branch
// claude/lucid-brown-7bm89z has them (include/terrain/generation/experimental_flags.hpp, not on
// main yet), the same names and types, so that the variants of the goldens (golden_support.hpp)
// are written against them. Replaced by that header, the row "volcanoes" moving into its table,
// before agent 45 asks for its merge.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace terrain {

// Every parameter a flag may set: the relief, the planet level, the water.
struct WorldParams {
    HeightfieldParams relief;
    PlanetParams planet;
    HydrologyParams water;
};

struct ExperimentalFlag {
    const char* name;
    const char* what;
    void (*apply)(WorldParams& params);
};

struct AppliedFlags {
    std::string text;                 // the names applied, sorted, joined by commas
    std::vector<std::string> unknown; // the names not in the table, as written
};

namespace experimental_detail {

// The rows of agent 45 only (agent 51 adds "seas" in its own table).
inline constexpr std::array kFlags = {
    ExperimentalFlag{"volcanoes", "the volcanoes (agent 45)",
                     [](WorldParams& p) { p.relief.volcanoes.enabled = true; }},
};

inline bool separator(char c) {
    return c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';';
}

} // namespace experimental_detail

// Applies to params the flags of text: names apart by commas or white space, their case
// ignored, empty ones skipped.
inline AppliedFlags applyExperimentalFlags(std::string_view text, WorldParams& params) {
    AppliedFlags out;
    std::vector<const ExperimentalFlag*> applied;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && experimental_detail::separator(text[at])) {
            ++at;
        }
        std::size_t end = at;
        while (end < text.size() && !experimental_detail::separator(text[end])) {
            ++end;
        }
        if (end > at) {
            std::string name(text.substr(at, end - at));
            std::transform(name.begin(), name.end(), name.begin(), [](char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            });
            const ExperimentalFlag* flag = nullptr;
            for (const ExperimentalFlag& f : experimental_detail::kFlags) {
                flag = name == f.name ? &f : flag;
            }
            if (flag == nullptr) {
                out.unknown.emplace_back(text.substr(at, end - at));
            } else if (std::find(applied.begin(), applied.end(), flag) == applied.end()) {
                applied.push_back(flag);
            }
        }
        at = end;
    }
    std::sort(applied.begin(), applied.end());
    for (const ExperimentalFlag* flag : applied) {
        flag->apply(params);
        out.text += (out.text.empty() ? "" : ",") + std::string(flag->name);
    }
    return out;
}

} // namespace terrain
