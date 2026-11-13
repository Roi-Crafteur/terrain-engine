// The experimental flags of the world (experimental_flags.hpp).
#include "terrain/generation/experimental_flags.hpp"

#include <algorithm>
#include <string>

namespace terrain {

namespace {

bool separator(char c) {
    return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
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

// The index of the row of written in table (its case ignored), or table.size().
std::size_t rowOf(const std::string& written, std::span<const ExperimentalFlag> table) {
    std::string name = written;
    std::transform(name.begin(), name.end(), name.begin(), lower);
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (name == table[i].name) {
            return i;
        }
    }
    return table.size();
}

AppliedFlags applyTo(std::string_view text, WorldParams* params,
                     std::span<const ExperimentalFlag> table) {
    AppliedFlags out;
    std::vector<std::size_t> rows;
    for (const std::string& name : namesOf(text)) {
        const std::size_t row = rowOf(name, table);
        if (row == table.size()) {
            out.unknown.push_back(name);
        } else if (std::find(rows.begin(), rows.end(), row) == rows.end()) {
            rows.push_back(row);
        }
    }
    // In the order of the table, sorted by name: the canonical text, whatever the order written.
    std::sort(rows.begin(), rows.end());
    for (const std::size_t row : rows) {
        if (params != nullptr) {
            table[row].apply(*params);
        }
        out.text += (out.text.empty() ? "" : ",") + std::string(table[row].name);
    }
    return out;
}

} // namespace

std::span<const ExperimentalFlag> experimentalFlags() {
    // One row per flag, sorted by name: {"name", "what it does (agent)", [](WorldParams& p) {...}}.
    static const std::vector<ExperimentalFlag> flags = {
        {"seas", "the floors of the seas (agent 51)",
         [](WorldParams& p) { p.water.seas.enabled = true; }},
    };
    return flags;
}

AppliedFlags applyExperimentalFlags(std::string_view text, WorldParams& params,
                                    std::span<const ExperimentalFlag> table) {
    return applyTo(text, &params, table);
}

std::string canonicalFlags(std::string_view text, std::span<const ExperimentalFlag> table) {
    return applyTo(text, nullptr, table).text;
}

std::string knownFlagNames(std::span<const ExperimentalFlag> table) {
    std::string names;
    for (const ExperimentalFlag& flag : table) {
        names += (names.empty() ? "" : ", ") + std::string(flag.name);
    }
    return names;
}

} // namespace terrain
