#pragma once

// Internal to the volcanoes: the breach of a caldera (agent 45). Not a straight notch: a notch
// through its wall down to the bottom of the breach, then a gorge cut down its flank (Aniakchak,
// the Gates), narrow at its lip, wider and deeper down, winding, its two walls each its own
// width and steepness along it, its flat floor always falling (it follows the flank less its depth,
// which fades over a length the flank falls by twice it). At sea, the strait into a drowned
// caldera: the same gorge, its floor flat under the sea out to the sea. Deterministic.

#include "volcano_relief.hpp"

namespace terrain::volcano_gorge {

// Where a point lies around the breach, in the frame of its volcano.
struct Place {
    double r = 0.0;     // from the center of the crater, meters
    double turns = 0.0; // its azimuth
    double lip = 0.0;   // the radius of the rim along the azimuth of the breach
};

// Cuts the breach of site into point (its height, its flooding at sea). footprint: the side of
// the square a sample stands for (0 for a point): where the gorge is narrower than it, its mean.
// Returns how much of the point lies in the gorge, in [0, 1]: the ravines of the flank spare it.
double cut(const VolcanoSite& site, const volcano_relief::Shape& shape, const Place& at,
           double footprint, volcano_relief::Point& point);

// Its gorge from its seed: its widths, length and bends (its depths come with the details).
[[nodiscard]] volcano_relief::GorgeShape gorgeOf(const VolcanoSite& site);

// The bottom of its breach and the floor of its gorge, read with the details from the surface of
// the volcano without them over ground. At sea, the bottom of the strait, 20 to 80 m under the
// sea. On land, the bottom high in the wall at its lip (where a lake spills), under every other
// point of the rim; the floor of the gorge along its axis (GorgeShape::floor): from that bottom,
// the flank less its depth (from its depth at the lip to 1.6 to 2.4 times it, 0.6 of the depth of
// the crater at most), never rising, out to where the flank comes down to it. A gorge too deep to
// come out on its flank is made shallower.
void readBreach(const VolcanoSite& site, volcano_relief::Detail& detail,
                const volcano_relief::GroundAt& ground);

} // namespace terrain::volcano_gorge
