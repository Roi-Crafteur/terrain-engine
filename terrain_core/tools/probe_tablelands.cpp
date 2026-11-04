// The section tablelands of terrain_probe (probe_sections.hpp): the provinces of the plateaus and
// of the karst at the point (agent 46, tablelands.hpp) and the pre-climate that places them
// (pre_climate.hpp).
#include "probe_sections.hpp"

#include "terrain/generation/pre_climate.hpp"
#include "terrain/generation/tablelands.hpp"

namespace probe {

Record tablelandsSection(const ProbeWorld& w, const Point& p) {
    const terrain::Tablelands* t = w.field().tablelands();
    Record r;
    if (t == nullptr) {
        return r.flag("available", false)
            .none("tableland")
            .none("karst")
            .none("arid")
            .none("tropical")
            .none("carbonate")
            .none("preHumidity")
            .none("preTemperature")
            .none("prePrecipitation");
    }
    const terrain::TablelandPoint at = w.field().tablelandAt(p.x, p.z);
    const terrain::PreClimate& c = t->climate();
    return r.flag("available", true)
        .number("tableland", at.tableland, 4)
        .number("karst", at.karst, 4)
        .number("arid", at.arid, 4)
        .number("tropical", at.tropical, 4)
        .number("carbonate", at.carbonate, 4)
        .number("preHumidity", c.humidityAt(p.x, p.z), 4)
        .number("preTemperature", c.temperatureAt(p.x, p.z), 2, "C")
        .number("prePrecipitation", c.precipitationAt(p.x, p.z), 1, "mm/yr");
}

} // namespace probe
