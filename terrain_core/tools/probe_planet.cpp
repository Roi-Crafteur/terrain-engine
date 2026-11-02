// The section planet of terrain_probe (probe_sections.hpp): the cell of the planet level holding
// the point, its climate and its water.
#include "probe_sections.hpp"

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_level.hpp"

namespace probe {

namespace {

constexpr const char* kKeys[] = {"cell",        "column",        "row",       "centerX",
                                 "centerZ",     "elevation",     "cellElevation",
                                 "temperature", "precipitation", "pet",       "runoff",
                                 "humidity",    "waterSurface",  "ocean",     "frozen",
                                 "lake",        "lakeLevel",     "lakePlaya", "river",
                                 "discharge",   "strahler",      "windHeading"};

void lakeOf(const terrain::PlanetLevel& planet, std::int32_t lake, Record& r) {
    if (lake < 0) {
        r.none("lake").none("lakeLevel").none("lakePlaya");
        return;
    }
    const terrain::PlanetLake& l = planet.lakes()[static_cast<std::size_t>(lake)];
    r.integer("lake", lake).number("lakeLevel", l.level, 2, "m").flag("lakePlaya", l.playa);
}

} // namespace

Record planetSection(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::PlanetLevel* planet = w.planet();
    r.flag("available", planet != nullptr);
    if (planet == nullptr) {
        for (const char* key : kKeys) {
            r.none(key);
        }
        return r;
    }
    const terrain::PlanetGrid& grid = planet->grid();
    const std::size_t cell = terrain::planetCellAt(grid, p.x, p.z);
    const int column = grid.column(cell);
    const int row = grid.row(cell);
    const terrain::PlanetSample s = planet->sampleAt(p.x, p.z);
    r.integer("cell", static_cast<std::int64_t>(cell))
        .integer("column", column)
        .integer("row", row)
        .number("centerX", grid.centerX(column), 1, "m")
        .number("centerZ", grid.centerZ(row), 1, "m")
        .number("elevation", s.elevation, 2, "m")
        .number("cellElevation", planet->elevation(cell), 0, "m")
        .number("temperature", s.temperature, 2, "C")
        .number("precipitation", s.precipitation, 1, "mm/yr")
        .number("pet", s.pet, 1, "mm/yr")
        .number("runoff", s.runoff, 1, "mm/yr")
        .number("humidity", planet->humidityAt(p.x, p.z), 4)
        .number("waterSurface", s.waterSurface, 2, "m")
        .flag("ocean", s.ocean)
        .flag("frozen", s.frozen);
    lakeOf(*planet, s.lake, r);
    const terrain::PlanetCells::RiverCell* river = planet->cells().riverAt(cell);
    r.flag("river", river != nullptr);
    if (river != nullptr) {
        r.number("discharge", s.discharge, 2, "m3/s").integer("strahler", s.strahler);
    } else {
        r.none("discharge").none("strahler");
    }
    if (s.wind.x != 0.0 || s.wind.z != 0.0) {
        r.number("windHeading", headingOf(s.wind.x, s.wind.z), 1, "deg"); // where it blows
    } else {
        r.none("windHeading");
    }
    return r;
}

} // namespace probe
