#pragma once

// The inlets and stacks of the coasts (agent 33, coast.hpp), internal to terrain_core: what one
// cell of their lattices holds, built from the world as they read it, and what each makes of a
// point. An inlet is a tube of sea along an axis rooted out in the sea of the geography and going
// inland up its coast signal: the distance to its water, the floor in it and the share of the
// plunging mountains on its sides. A stack is a dome of rock out at sea off the capes.

#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace terrain::coast_inlets {

// What the inlets read of the world at a canonical point: the coast signal of the geography, the
// weights of the kinds of coast there and the share of strata of the rock.
struct Site {
    double coast = 0.0;
    std::array<float, kCoastKindCount> weights{};
    float strata = 0.5f;
    float top = 0.0f;  // the top of the faces of its shore kinds (coast_shape::shoreTop), meters
};

// The world as the inlets read it. clearance: how far the valleys, ponds and lakes of the rivers
// keep from a canonical point (Hydrology::clearance over the relief without the coasts), null
// without rivers.
struct World {
    WorldSize size;
    std::uint64_t seed = 0;
    double band = 30'000.0; // GeographyParams::coastRamp
    std::function<double(double x, double z)> coast;
    // The height of the world there, its coasts shaped but neither inlets nor rivers cut in it.
    std::function<double(double x, double z)> height;
    std::function<Site(double x, double z)> site;
    std::function<double(double x, double z)> clearance;
};

// A point of the axis of an inlet: where (near its root, not wrapped), the half width of its
// water and the depth of its floor on the axis there.
struct AxisPoint {
    double x = 0.0;
    double z = 0.0;
    float half = 0.0f;
    float depth = 0.0f;
};

struct Inlet {
    InletKind kind = InletKind::Cove;
    std::vector<AxisPoint> axis; // from its root out at sea to its head
    float wall = 0.0f;           // meters over which the plunging mountains rise from its sides
    // Its water and walls, near its root (not wrapped).
    double minX = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;
    CoastSite site;
};

// A stack: a dome of rock, its foot radius meters from its middle at least (more on its lobes),
// the lobes turned by phase (in [0, 1)).
struct Stack {
    double x = 0.0; // canonical
    double z = 0.0;
    float radius = 0.0f;
    float height = 0.0f;
    float phase = 0.0f;
};

// What the inlets and stacks make of a point. The coast signal of the world there is that of the
// geography no higher than water times the signal per meter, no lower than islet times it.
struct Effect {
    double water = std::numeric_limits<double>::infinity(); // to their water, m, < 0 inside
    double floor = 0.0;                                      // meters, at most 0
    double rise = 1.0; // the share of the plunging mountains: 0 at their water, 1 beyond walls
    double islet = -std::numeric_limits<double>::infinity(); // into the stacks, m, > 0 inside
    double dome = 0.0;                                        // the height of the stacks
    double deep = 0.0; // the depth of their floor on their axis, meters
    // In their water, as rise on their sides: 0 at their shore, 1 a wall into the water.
    double sink = 0.0;
    // In the reach of the fjords: how far the plunging mountains rise from their water rather than
    // from the shore of the geography (1 in their water and near it, 0 from their wall on).
    double glen = 0.0;
    // The rock of the reefs the point lies furthest into (that of islet): its middle, its radius
    // toward the point (its lobes), its height, the top of its dome over the point (its rough
    // relief) and the phase of its lobes (coast_faces: the faces of the rocks); the radius of a
    // stack (Stack::radius: its spire, coast_reefs::spireOf).
    double rockX = 0.0;
    double rockZ = 0.0;
    double rockRadius = 0.0;
    double rockSize = 0.0;
    double rockHeight = 0.0;
    double rockTop = 0.0;
    double rockPhase = 0.0;
    // How far the point lies into the next rock of the reefs (as islet: the second furthest).
    double nextIslet = -std::numeric_limits<double>::infinity();
    // The stack whose spire may stand over the point (within its reach, rock_spires::Shape::
    // reach: the furthest into it), whatever rock the point lies furthest into: its middle, its
    // radius and its height (radius 0: none).
    double spireX = 0.0;
    double spireZ = 0.0;
    double spireSize = 0.0;
    double spireHeight = 0.0;
    double spireIn = -std::numeric_limits<double>::infinity();
};

// The coast signal per meter of the inlets and stacks (about the gradient of the coast signal).
inline constexpr double kSignalPerMeter = 3.0;

[[nodiscard]] double signalOf(double coast, const Effect& e);
// The distance to the shore of the world from the distance to the shore of the geography (base,
// meters, signed as the coast signal): that to their water where nearer, that into the stacks.
// Never below the true distance where their signal gives way, never folded.
[[nodiscard]] double distanceOf(double base, const Effect& e);

// The noise of the cut of the coves (kCutAmplitude meters of it on their distance to their water):
// no flat facet on their walls and their capes, a ragged shore.
[[nodiscard]] noise::FbmOctaves makeCut(std::uint64_t seed, const WorldSize& world);

void addInlet(const Inlet& inlet, double x, double z, const WorldSize& world,
              const noise::FbmOctaves& cut, Effect& e);

// A distance w as the signal of the inlets and rocks reads it: itself up to near, then growing to
// infinity at far, so that their signal gives way to that of the geography before far (C1 at
// near).
[[nodiscard]] double reachOf(double w, double near, double far);

// The lattices of the inlets (coast_lattice.hpp): the large one holds the fjords and the rias, the
// small one the calanques, the coves and the stacks.
enum class Lattice : std::uint8_t { Large, Small };

// The numbers of a cell of a lattice, in [0, 1): the same for the same world, lattice and cell.
class Draws {
public:
    Draws(const World& w, Lattice lattice, std::int64_t i, std::int64_t j);
    double next();

private:
    std::uint64_t state_;
};

// The shape drawn for an inlet: its widest half, depth and length inland (meters), the wiggle of
// its axis (amplitude in turns, wavelength in meters, phase), and by kind the pocket of a
// calanque or the bulb of a cove (its widest over its mouth).
struct Shape {
    double half = 0.0;
    double depth = 0.0;
    double inland = 0.0;
    double wiggle = 0.0;
    double wavelength = 1.0;
    double phase = 0.0;
    double extra = 0.0;
};

[[nodiscard]] Shape shapeOf(InletKind kind, Draws& d);

// Moves (x, z) to target meters of coast signal by Newton steps; false where it does not get
// there.
[[nodiscard]] bool rootOf(const World& w, double target, double& x, double& z);

// The direction up the coast signal at (x, z), in turns.
[[nodiscard]] double inlandOf(const World& w, double x, double z);

// The inlet of a kind of that shape from its root (x, z) out at sea toward turns, its axis a
// point every 100 m (large lattice) or 25 m (small one), up to its length inland of its mouth;
// stopped before 12 km of coast signal, a return to the sea or 100 m (plus its widest half) from
// the corridor of a river or a lake. None (an empty axis) where it would not reach least meters
// inland.
[[nodiscard]] Inlet inletOf(const World& w, Lattice lattice, InletKind kind, const Shape& shape,
                            double x, double z, double turns, double least);

} // namespace terrain::coast_inlets
