#pragma once

// The places of terrain_export_rock (export_rock.cpp): the highest wall by the K2 seen near, far
// and where the tiles hand over to the horizon; the fall of 388 m (x -6,185,408, z 156,215) from
// its foot, its slot from above and its niche; a gorge near it; and views from straight above.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <string>
#include <vector>

namespace rock_tool {

// A view in perspective: the eye above meters over the ground (or the water) at (x, z), heading
// in turns clockwise from the north (-Z), tilted (the tangent of the elevation of the center row),
// height pixels high for a width of 1600 (scaled with the width).
struct Shot {
    std::string name;
    double x = 0.0;
    double z = 0.0;
    double above = 1.8;
    double heading = 0.0;
    double tilt = 0.0;
    int height = 900;
    double sunLeft = 0.3; // the sun, turns left of the axis of the view (behind it beyond 0.25)
    std::string note;
    double sunSine = 0.5; // the sine of the elevation of the sun
};

// A view from straight above, side meters wide, centered on (x, z).
struct TopShot {
    std::string name;
    double x = 0.0;
    double z = 0.0;
    double side = 300.0;
    double metersPerPixel = 0.25;
    std::string note;
};

struct Places {
    std::vector<Shot> shots;
    std::vector<TopShot> tops;
};

// The highest wall within 4 km of the K2 (view_places::highestWall on the relief): seen from 120
// m and from the end of its approach (view_wall), along it across the edge of the tiles (reach:
// the distance the tiles cover), and from above.
void addK2(const terrain::Heightfield& relief, double reach, Places& out);

// The stretch in steps whose lips come nearest to (x, z), within 3 km (its falls one after the
// other down the course, by the levels of its water): from its water down its foot, whole and its
// last face, its slot from 40 m above its water 150 m down its highest lip, up and down, and from
// above. False without one.
bool addFall(const terrain::Hydrology& hydrology, double x, double z, Places& out);

// The largest river in a gorge (as view_gorge of terrain_export_rivers) within 40 km of (x, z),
// no fall within 500 m: from beside its channel, looking down it, and from above. False without
// one.
bool addGorge(const terrain::Hydrology& hydrology, double x, double z, Places& out);

// The views of the fall of 388 m from where addFall set them before its slot wound (phase 12A,
// PA3 step 2): from its basin, its slot up and down from 40 m above its water, and from above, 300
// m and 600 m wide; the same eyes whatever the course does there since.
void addFallViews(Places& out);

} // namespace rock_tool
