#pragma once

// Internal to the volcanoes: what a volcano makes of the relief around it (agent 45). Its shape
// drawn from its seed; its details (the ground it stands on, its cones, maars and flows) read
// once from the relief without the volcanoes, then the point by point relief: the edifice over
// the ground, its crater or caldera dug, its ravines, its flows, sheets, cones and maars.
// In local coordinates around its center. Deterministic; the same bits whatever the thread that
// reads a detail first.

#include "terrain/generation/volcanoes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace terrain::volcano_relief {

// The shape a volcano draws from its seed, before any relief is read.
struct Shape {
    double cosAxis = 1.0; // its long axis (VolcanoSite::axis)
    double sinAxis = 0.0;
    // The outline of its foot: two harmonics (amplitudes, phases in turns) and a noise of cells
    // cells, stretching its radius by at most kOutline.
    double lobe1 = 0.0;
    double phase1 = 0.0;
    double lobe2 = 0.0;
    double phase2 = 0.0;
    double ragged = 0.0;
    int cells = 9;
    // The arms of the rift zones of a shield: how many (0 for the others), their azimuths
    // (turns), how far they stretch the radius and their half width (turns).
    int arms = 0;
    std::array<double, 3> armTurns{};
    std::array<double, 3> armReach{};
    double armWidth = 0.08;
    // The swell of the flanks (meters at the summit), over kilometers: no perfect dome.
    double swell = 0.0;
    // The summit off the center (meters, in the stretched frame), fading to the foot.
    double offsetU = 0.0;
    double offsetV = 0.0;
    double sharpness = 3.5;  // of the concave profile of a stratovolcano
    double smoothing = 40.0; // the width of the smooth maximum of the edifice and the ground
    // The rim of its crater: its noise; its breach (azimuth, half width in radians, bottom).
    double rimNoise = 0.08;
    bool breached = false;
    double breachAngle = 0.0;
    double breachWidth = 0.2;
    double breachBottom = 0.0;
    double wall = 0.5; // the share of the crater radius its wall takes
    // The amphitheater of a collapse, its azimuth.
    double collapseAngle = 0.0;
    // The ravines: how many around the mid flank, how deep (meters).
    int ravines = 0;
    double ravineDepth = 0.0;
};

// A cinder cone, a lava dome, a spatter cone or a maar, centered at (x, z) from the center of its
// volcano (meters).
struct Cone {
    double x = 0.0;
    double z = 0.0;
    double height = 0.0; // a maar: how deep it digs under the ground
    double radius = 0.0; // its foot (a maar: its rim)
    double crater = 0.0;
    double depth = 0.0;  // of its crater
    double ring = 0.0;   // a maar: its ring of tuff
    bool maar = false;
    bool breached = false; // a horseshoe cone, open toward breachAngle
    double breachAngle = 0.0;
    double reach = 0.0;    // nothing of it lies farther
};

// A flow of lava from (x, z) along (dirX, dirZ) over length meters, width wide (at its end),
// thickness thick, wandering by meander meters.
struct Flow {
    double x = 0.0;
    double z = 0.0;
    double dirX = 1.0;
    double dirZ = 0.0;
    double length = 0.0;
    double width = 0.0;
    double thickness = 0.0;
    double meander = 0.0;
    std::uint64_t seed = 0;
};

// A crater, caldera or maar that may hold a lake (volcano_water.hpp), (x, z) from the center of
// its volcano: its lowest ground; the most a lake in it may rise (the lowest point of its rim or
// the bottom of its breach, less a margin); on rays evenly spaced around it (turns from +x), the
// radius within which a lake may stand (where its inner wall rises to that height) and that of
// its crest (both empty for a maar: its radius all around); whether it holds water under a wet
// climate (drawn). The maars whose rims cut one another share one lake: their floor, spill and
// the point their climate is read at (wetX, wetZ), and group, the index of the first of them.
struct Basin {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    double floor = 0.0;
    double spill = 0.0;
    double wetX = 0.0;
    double wetZ = 0.0;
    std::vector<float> within;
    std::vector<float> crest;
    std::size_t group = 0;
    bool lake = false;
    bool maar = false;
};

// A hot spring, a geyser, a mud pot or a fumarole, (x, z) from the center of its volcano: the
// radius of its pool, pot or vent; the level of the water of its pool (absolute meters), the top
// of its terrace of sinter; how deep its pool or pot; how high the mound of a geyser.
struct Spring {
    double x = 0.0;
    double z = 0.0;
    GeothermalKind kind = GeothermalKind::HotSpring;
    double radius = 0.0;
    double level = 0.0;
    double depth = 0.0;
    double mound = 0.0;
};

// A top of a volcano, (x, z) from its center: the highest point of its summit or the rim of its
// crater, or of a cone, absolute meters over the real ground (the horizon shows it within the
// cell of a sample however coarse: Heightfield::seenAt).
struct Top {
    double x = 0.0;
    double z = 0.0;
    double height = 0.0;
};

// Where the springs of a volcano rise: their ground (center from the center of the volcano,
// radius; 0 for none).
struct Zone {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
};

struct Detail {
    Shape shape;
    // The ground the edifice stands on: the mean of the relief around (on land), its floor (at
    // sea); for a field, its mean minus the regional ground at its center (sheetOffset).
    double base = 0.0;
    double sheetOffset = 0.0;
    std::vector<Cone> cones;
    std::vector<Flow> flows;
    std::vector<Basin> basins;
    std::vector<Spring> springs;
    Zone zone;
    std::vector<Top> tops;
};

// The relief without the volcanoes and the coasts at a point (any image of it): its height and its
// regional ground.
struct Ground {
    double height = 0.0;
    double regional = 0.0;
};
using GroundAt = std::function<Ground(double, double)>;

// The shape of a site, from its seed alone.
[[nodiscard]] Shape shapeOf(const VolcanoSite& site);
// The ground an edifice stands on (Detail::base on land, its floor at sea), and how high its
// profile rises over it: for a caldera, the summit of the cone it collapsed from, so that its rim
// stands at its height (VolcanoSite::height).
[[nodiscard]] double edificeBase(const VolcanoSite& site, const Detail& detail);
[[nodiscard]] double edificeRise(const VolcanoSite& site, const Shape& shape);
// The profile of its edifice at rho (1 at the summit, 0 at the foot).
[[nodiscard]] double profileOf(const VolcanoSite& site, const Shape& shape, double rho);

// What the volcanoes make of a point, gathered over those that reach it.
struct Point {
    double height = 0.0;   // the ground, in and out
    double regional = 0.0; // the regional ground under it (HeightSample::regional)
    float cover = 0.0f;    // [0, 1]: how much of the ground is their work
    VolcanicGround ground = VolcanicGround::None; // the ground they left (volcano_ground.hpp)
    float groundShare = 0.0f; // [0, 1]: how bare it lies
    bool enclosed = false; // in a crater whose rim stays above the sea all around
    bool flooded = false;  // in a caldera the sea floods through its strait, or in that strait
    bool touched = false;  // a volcano changed the height
};

// Applies the volcano of site and detail to the point at (dx, dz) from its center. footprint: the
// side of the square a sample stands for (0 for a point): the details smaller than a few tenths of
// it give way to their mean.
void apply(const VolcanoSite& site, const Detail& detail, double dx, double dz, double footprint,
           Point& point);
// The surface of the volcano at (dx, dz) over the ground ground, before its flows, cones and
// maars (the flows find their course over it).
[[nodiscard]] double surfaceBeforeFlows(const VolcanoSite& site, const Detail& detail, double dx,
                                        double dz, const Ground& ground);

// Its details from its shape and the relief around it (volcano_details.cpp). wx, wz: its center.
[[nodiscard]] Detail detailOf(const VolcanoSite& site, const GroundAt& ground);

// The details of the volcanoes of a Heightfield, each read once on demand, from any thread.
class Details {
public:
    explicit Details(std::size_t count);
    Details(const Details&) = delete;
    Details& operator=(const Details&) = delete;

    [[nodiscard]] const Detail& get(std::size_t index, const VolcanoSite& site,
                                    const GroundAt& ground) const;

private:
    std::unique_ptr<std::once_flag[]> flags_;
    std::unique_ptr<Detail[]> details_;
};

} // namespace terrain::volcano_relief
