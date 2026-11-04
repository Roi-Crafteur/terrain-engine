// The section sea of terrain_probe (probe_sections.hpp): what the seas of agent 51 (seas.hpp) make
// of the point, from the relief without them: the zone, the distance to the shore, the floor
// before and after, the continental margin, the crust of the ocean (its ridge, age and spreading
// rate), the oceanic plateaus, the trench and its outer rise, the hills, the fracture zones and the
// seamounts.
#include "probe_sections.hpp"

#include "terrain/generation/seas.hpp"

#include <string_view>

namespace probe {

namespace {

constexpr const char* kKeys[] = {"zone",          "fade",     "shore",   "before",  "floor",
                                 "margin",        "active",   "crust",   "plateau", "ridge",
                                 "ridgeDistance", "age",      "rate",    "trench",  "outerRise",
                                 "hills",         "fracture", "seamount"};

} // namespace

Record seaSection(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::Seas* seas = w.hydrology() != nullptr ? w.hydrology()->seas() : nullptr;
    r.flag("available", seas != nullptr);
    if (seas == nullptr) {
        for (const char* key : kKeys) {
            r.none(key);
        }
        return r;
    }
    // From the relief alone: at sea, the world without its seas (its rivers never take the sea).
    const terrain::SeaPoint s = seas->at(p.x, p.z, w.relief().sampleAt(p.x, p.z), 0.0);
    r.flag("zone", s.zone);
    if (!s.zone) {
        for (const char* key : kKeys) {
            if (std::string_view(key) != "zone") {
                r.none(key);
            }
        }
        return r;
    }
    r.number("fade", s.fade, 4)
        .number("shore", s.shore, 0, "m")
        .number("before", s.before, 2, "m")
        .number("floor", s.floor, 2, "m")
        .number("margin", s.margin, 2, "m")
        .number("active", s.active, 4)
        .number("crust", s.crust, 2, "m")
        .number("plateau", s.plateau, 4);
    if (s.ridge >= 0) {
        r.integer("ridge", s.ridge)
            .number("ridgeDistance", s.ridgeDistance, 0, "m")
            .number("age", s.age, 2, "Myr")
            .number("rate", s.rate, 4);
    } else {
        r.none("ridge").none("ridgeDistance").none("age").none("rate");
    }
    return r.number("trench", s.trench, 2, "m")
        .number("outerRise", s.outerRise, 2, "m")
        .number("hills", s.hills, 2, "m")
        .number("fracture", s.fracture, 2, "m")
        .number("seamount", s.seamount, 2, "m");
}

} // namespace probe
