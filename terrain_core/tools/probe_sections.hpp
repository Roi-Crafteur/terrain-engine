#pragma once

// The sections of terrain_probe: what the core knows of a point, one section per module, in the
// order of the table (sections()). Each section gives the same keys at every point (absent values
// as such), so the schema of the JSON never depends on the point.
//
// To add a section (a module of its own: a kingdom, a people, the Mana...): write its function
// in tools/probe_<name>.cpp, declare it below, add one line to the table in probe_sections.cpp
// and the file to terrain_probe_lib in terrain_core/CMakeLists.txt. Nothing else changes.

#include "probe_options.hpp"
#include "probe_record.hpp"
#include "probe_world.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water_kind.hpp"

#include <string>
#include <vector>

namespace probe {

// What every section of a point starts from, computed once.
struct Point {
    double x = 0.0; // canonical (wrapCoordinate)
    double z = 0.0;
    terrain::HeightSample sample; // the world (the relief with its water): field().sampleAt
    float ground = 0.0f;          // field().heightAt: the ground of the Unreal frame
    float relief = 0.0f;          // relief().heightAt: before the valleys of the rivers
};

using SectionProbe = Record (*)(const ProbeWorld& world, const Point& point);

struct Section {
    const char* name;
    const char* what; // one line for --list-sections
    SectionProbe probe;
    bool line; // probed along a line unless --sections says otherwise
};

// The table of the sections, in their order.
[[nodiscard]] const std::vector<Section>& sections();
[[nodiscard]] std::vector<std::string> sectionNames();
// The sections of --sections (in the order of the table), or the defaults: every section for
// points, those marked line along a line.
[[nodiscard]] std::vector<const Section*> chosenSections(const Options& options, bool line);

[[nodiscard]] Point pointAt(const ProbeWorld& world, double x, double z);
// A point: its canonical x and z, then each section under its name.
[[nodiscard]] Record probePoint(const ProbeWorld& world, double x, double z,
                                const std::vector<const Section*>& chosen);

// The points of a line, as given (unwrapped): every step meters from its start, then its end
// exactly unless the last step lands on it; distance along it in meters.
struct LinePoint {
    double distance = 0.0;
    double x = 0.0;
    double z = 0.0;
};
[[nodiscard]] std::vector<LinePoint> linePoints(const Line& line);
// The line: from, to, step, length and its points (distance, then as probePoint).
[[nodiscard]] Record probeLine(const ProbeWorld& world, const Line& line,
                               const std::vector<const Section*>& chosen);

// The whole answer of a probe: the tool, the world, the planet, then the points or the line.
[[nodiscard]] Record probeDocument(const ProbeWorld& world, const std::string& planetStatus);
// The text of a line: one row per point, the scalar fields as columns.
[[nodiscard]] std::string lineTable(const Record& line);

// Helpers of the sections.
[[nodiscard]] const char* kindName(terrain::WaterKind kind);
// Degrees clockwise from the north (-z) of the direction (dx, dz), in [0, 360).
[[nodiscard]] double headingOf(double dx, double dz);
// Degrees of the slope of a gradient (rise over run).
[[nodiscard]] double slopeDegrees(double gradient);
// The canonical coordinates of (x, z).
[[nodiscard]] double wrapX(const ProbeWorld& world, double x);
[[nodiscard]] double wrapZ(const ProbeWorld& world, double z);

// The sections (one file each).
[[nodiscard]] Record reliefSection(const ProbeWorld& world, const Point& point);    // probe_relief
[[nodiscard]] Record geographySection(const ProbeWorld& world, const Point& point); // probe_relief
[[nodiscard]] Record coastSection(const ProbeWorld& world, const Point& point);     // probe_relief
[[nodiscard]] Record planetSection(const ProbeWorld& world, const Point& point);    // probe_planet
[[nodiscard]] Record waterSection(const ProbeWorld& world, const Point& point);     // probe_water
[[nodiscard]] Record renderSection(const ProbeWorld& world, const Point& point);    // probe_water
[[nodiscard]] Record fallsSection(const ProbeWorld& world, const Point& point);     // probe_falls
[[nodiscard]] Record rockSection(const ProbeWorld& world, const Point& point);      // probe_rock
[[nodiscard]] Record surfaceSection(const ProbeWorld& world, const Point& point);   // probe_surface

} // namespace probe
