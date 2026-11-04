#pragma once

// Internal to the volcanoes: the ground they leave at a point (agent 45, VolcanicGround), decided
// by the core whatever the biome. Each part of a volcano offers its ground with a share (how bare
// it lies, 1 where nothing grows over it); a point keeps the ground offered with the largest share.
//   - cones of cinders and the craters of the active cones: scoria;
//   - the upper slopes of the stratovolcanoes, the floors of the calderas, the rings of the maars,
//     the plateaus of ash: ash;
//   - the shields and the walls of the calderas, the old flows and sheets: basalt;
//   - the flows and sheets of the active volcanoes: cooled lava;
//   - the zones of the springs: geothermal ground (sinter, altered ground).

#include "volcano_relief.hpp"

namespace terrain::volcano_ground {

// Offers ground at share (clamped to [0, 1]) to point.
void offer(volcano_relief::Point& point, VolcanicGround ground, double share);

// The ground of an edifice at rho of its foot, r meters from its summit, where it holds
// dominance of the relief (volcano_relief.cpp).
void edifice(const VolcanoSite& site, double rho, double r, double dominance,
             volcano_relief::Point& point);
// The ground of a sheet of lava where it covers cover of the point, of a flow where its mask is
// mask, of a cinder cone or a maar where it covers cover.
void sheet(const VolcanoSite& site, double cover, volcano_relief::Point& point);
void flow(const VolcanoSite& site, double mask, volcano_relief::Point& point);
void cone(const VolcanoSite& site, bool maar, double cover, volcano_relief::Point& point);
// The ground of the zone of the springs of a detail at (dx, dz) from the center of its volcano.
void springs(const volcano_relief::Detail& detail, double dx, double dz,
             volcano_relief::Point& point);

} // namespace terrain::volcano_ground
