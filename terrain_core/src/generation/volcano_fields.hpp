#pragma once

// Internal to the volcanoes: what lies on and around their edifices and makes their fields (agent
// 45): the sheets of lava filling the hollows of a field, the flows with their steep fronts and
// levees, the cinder cones (breached or not), the lava domes and the maars dug under the ground.

#include "volcano_relief.hpp"

namespace terrain::volcano_fields {

// The sheet of lava of a field of lava or a row of fissures at (dx, dz) from its center: the
// hollows filled up to its level (the regional ground, the offset of the field, its thickness),
// the hills above it left as islands, its lobed edges steep, its surface rough. footprint: the
// roughness smaller than a few tenths of it gives way to its mean (0).
void applySheet(const VolcanoSite& site, const volcano_relief::Detail& detail, double dx,
                double dz, double footprint, volcano_relief::Point& point);
// The flows of the volcano at (dx, dz): their thickness over the ground, their levees, their
// lobed fronts, their rough crust.
void applyFlows(const VolcanoSite& site, const volcano_relief::Detail& detail, double dx,
                double dz, double footprint, volcano_relief::Point& point);
// Its cones, domes and maars at (dx, dz).
void applyCones(const VolcanoSite& site, const volcano_relief::Detail& detail, double dx,
                double dz, volcano_relief::Point& point);

} // namespace terrain::volcano_fields
