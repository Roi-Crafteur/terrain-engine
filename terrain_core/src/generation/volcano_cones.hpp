#pragma once

// Internal to the volcanoes: the small forms a volcano draws from its seed, before any relief is
// read (agent 45): its satellite cones and lava domes, the cones along the fissures of its
// fields, its maars, and its flows (their course, cut later where the ground rises:
// volcano_details.cpp). Every one stays within the reach of its volcano.

#include "volcano_relief.hpp"

namespace terrain::volcano_cones {

// The cones, domes and maars of site into detail (its shape set).
void addCones(const VolcanoSite& site, volcano_relief::Detail& detail);
// Its flows into detail (its shape and cones set).
void addFlows(const VolcanoSite& site, volcano_relief::Detail& detail);

} // namespace terrain::volcano_cones
