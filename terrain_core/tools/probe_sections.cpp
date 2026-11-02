// The table of the sections of terrain_probe, its points and its lines (probe_sections.hpp).
#include "probe_sections.hpp"

#include "terrain/det_math.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace probe {

namespace {

constexpr double kDegreesPerRadian = 57.295779513082320876798154814105;

} // namespace

const std::vector<Section>& sections() {
    // One line per section, in the order of the output.
    static const std::vector<Section> table = {
        {"relief", "relief and ground (the ground of the Unreal frame), slope, layers",
         &reliefSection, true},
        {"geography", "the planetary geography: coast distance, belts, rifts, relief",
         &geographySection, false},
        {"planet", "the cell of the planet level: climate, lake, river cell, discharge",
         &planetSection, false},
        {"water", "kind, level, depth, valley of the rivers, lake, current", &waterSection, true},
        {"falls", "the falls near the point and the corridor of steps", &fallsSection, false},
        {"rock", "slope and the solid and empty intervals of the column", &rockSection, true},
        {"coast", "distance and heading to the sea, rugged share, coast type", &coastSection,
         false},
        {"surface", "biome, material, plants, local climate, blend weights", &surfaceSection,
         true},
        {"render", "what Unreal gets: the vertices of the tiles of LOD 0 to 3, the horizon",
         &renderSection, false},
    };
    return table;
}

std::vector<std::string> sectionNames() {
    std::vector<std::string> names;
    for (const Section& s : sections()) {
        names.emplace_back(s.name);
    }
    return names;
}

std::vector<const Section*> chosenSections(const Options& o, bool line) {
    std::vector<const Section*> out;
    for (const Section& s : sections()) {
        const bool asked = std::find(o.sections.begin(), o.sections.end(), s.name) !=
                           o.sections.end();
        if (o.sections.empty() ? (!line || s.line) : asked) {
            out.push_back(&s);
        }
    }
    return out;
}

double wrapX(const ProbeWorld& w, double x) { return terrain::wrapCoordinate(x, w.world().x); }
double wrapZ(const ProbeWorld& w, double z) { return terrain::wrapCoordinate(z, w.world().z); }

Point pointAt(const ProbeWorld& w, double x, double z) {
    Point p;
    p.x = wrapX(w, x);
    p.z = wrapZ(w, z);
    p.sample = w.field().sampleAt(p.x, p.z);
    p.ground = w.field().heightAt(p.x, p.z);
    p.relief = w.relief().heightAt(p.x, p.z);
    return p;
}

namespace {

Record pointRecord(const ProbeWorld& w, double x, double z,
                   const std::vector<const Section*>& chosen, Record start) {
    const Point p = pointAt(w, x, z);
    start.number("x", p.x, 2, "m").number("z", p.z, 2, "m");
    for (const Section* s : chosen) {
        start.add(s->name, s->probe(w, p));
    }
    return start;
}

Record xz(double x, double z) {
    Record r;
    r.number("x", x, 2, "m").number("z", z, 2, "m");
    return r;
}

} // namespace

Record probePoint(const ProbeWorld& w, double x, double z,
                  const std::vector<const Section*>& chosen) {
    return pointRecord(w, x, z, chosen, {});
}

std::vector<LinePoint> linePoints(const Line& l) {
    const double dx = l.x2 - l.x1;
    const double dz = l.z2 - l.z1;
    const double length = terrain::dmath::hypot(dx, dz);
    std::vector<LinePoint> out;
    if (!(length > 0.0)) {
        out.push_back({0.0, l.x1, l.z1});
        return out;
    }
    const auto steps = static_cast<std::size_t>(std::floor(length / l.step));
    for (std::size_t i = 0; i <= steps; ++i) {
        const double d = static_cast<double>(i) * l.step;
        if (d >= length) {
            break; // the end itself, below
        }
        const double t = d / length;
        out.push_back({d, l.x1 + dx * t, l.z1 + dz * t});
    }
    out.push_back({length, l.x2, l.z2});
    return out;
}

Record probeLine(const ProbeWorld& w, const Line& l, const std::vector<const Section*>& chosen) {
    const std::vector<LinePoint> points = linePoints(l);
    std::vector<Record> items;
    items.reserve(points.size());
    for (const LinePoint& p : points) {
        Record start;
        start.number("distance", p.distance, 2, "m");
        items.push_back(pointRecord(w, p.x, p.z, chosen, std::move(start)));
    }
    Record r;
    r.add("from", xz(l.x1, l.z1))
        .add("to", xz(l.x2, l.z2))
        .number("step", l.step, 2, "m")
        .number("length", terrain::dmath::hypot(l.x2 - l.x1, l.z2 - l.z1), 2, "m")
        .list("points", std::move(items));
    return r;
}

Record probeDocument(const ProbeWorld& w, const std::string& planetStatus) {
    const Options& o = w.options();
    Record r;
    r.text("tool", "terrain_probe")
        .integer("format", 1)
        .add("world", worldRecord(w))
        .add("planet", planetRecord(w, planetStatus));
    if (o.line) {
        return r.add("line", probeLine(w, *o.line, chosenSections(o, true)));
    }
    const std::vector<const Section*> chosen = chosenSections(o, false);
    std::vector<Record> points;
    for (const auto& [x, z] : o.points) {
        points.push_back(probePoint(w, x, z, chosen));
    }
    return r.list("points", std::move(points));
}

std::string lineTable(const Record& line) {
    const Field* points = line.find("points");
    const auto* items = points ? std::get_if<std::vector<Record>>(&points->value) : nullptr;
    if (items == nullptr || items->empty()) {
        return {};
    }
    std::vector<std::vector<std::pair<std::string, std::string>>> rows;
    for (const Record& p : *items) {
        rows.push_back(flatten(p));
    }
    std::vector<std::size_t> width;
    for (const auto& [key, value] : rows.front()) {
        width.push_back(key.size());
    }
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < row.size() && c < width.size(); ++c) {
            width[c] = std::max(width[c], row[c].second.size());
        }
    }
    const auto cell = [&](std::string& out, std::size_t c, const std::string& text) {
        out += std::string(width[c] - std::min(width[c], text.size()), ' ') + text +
               (c + 1 < width.size() ? "  " : "\n");
    };
    std::string out;
    for (std::size_t c = 0; c < width.size(); ++c) {
        cell(out, c, rows.front()[c].first);
    }
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < width.size(); ++c) {
            cell(out, c, c < row.size() ? row[c].second : "");
        }
    }
    return out;
}

const char* kindName(terrain::WaterKind kind) {
    switch (kind) {
    case terrain::WaterKind::Ocean:
        return "sea";
    case terrain::WaterKind::River:
        return "river";
    case terrain::WaterKind::Lake:
        return "lake";
    case terrain::WaterKind::SaltLake:
        return "salt lake";
    case terrain::WaterKind::None:
        break;
    }
    return "dry";
}

double headingOf(double dx, double dz) {
    double degrees = terrain::dmath::atan2(dx, -dz) * kDegreesPerRadian;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees >= 360.0 ? 0.0 : degrees;
}

double slopeDegrees(double gradient) {
    return terrain::dmath::atan(gradient) * kDegreesPerRadian;
}

} // namespace probe
