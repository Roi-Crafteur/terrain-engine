#pragma once

// PROVISIONAL (agent 46): the option --flags <a,b,...> of the tools, switching on the relief of wave
// 9 that is off by default, until the common flags of agent 51 (generation/experimental_flags.hpp)
// replace this file with the same option, the same names and the same syntax. Known names:
// tablelands (the plateaus, mesas, canyons and the high surfaces of the plateaus) and karst
// (GeographyParams::tablelands and ::karst).

#include "terrain/generation/heightfield.hpp"

#include <string>
#include <string_view>

namespace terrain_tools {

// Switches on the flags of list (names separated by commas) in params. False for an empty list or
// an unknown name (then in bad), params unchanged.
inline bool setReliefFlags(std::string_view list, terrain::HeightfieldParams& params,
                           std::string& bad) {
    terrain::HeightfieldParams out = params;
    bool any = false;
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::string_view name = list.substr(0, comma);
        if (name == "tablelands") {
            out.geography.tablelands = true;
        } else if (name == "karst") {
            out.geography.karst = true;
        } else {
            bad = std::string(name);
            return false;
        }
        any = true;
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    if (!any) {
        bad.clear();
        return false;
    }
    params = out;
    return true;
}

} // namespace terrain_tools
