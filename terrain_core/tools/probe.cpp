// terrain_probe: everything the core knows of points of the world, or along a line, one section
// per module (probe_sections.hpp): the relief and the ground (the ground of the Unreal frame),
// the geography, the cell of the planet level (climate, discharge), the water (kind, level,
// depth, valley, lake, current), the falls around and the corridor of steps, the rock of the
// column, the coast, the surface (biome, material, plants) and what the renderers get (the
// vertices of the tiles of LOD 0 to 3, the horizon). Readable text, or JSON with --json (a stable
// schema: the same keys at every point). It replaces terrain_probe_water.
//
// Without --planet: the relief and the sea only, no rivers nor lakes. --planet reads the planet
// level from the file, or builds it and writes it there when the file does not hold the level of
// the seed and the world (missing, stale or damaged). --planet-check only reads a file and never
// writes it: its status ("ok", "missing", "stale", "damaged", "newer", "unreadable") and the
// checksum of its level; exit code 2 unless ok.
//
// Usage: terrain_probe [--planet <file>] [--seed <seed>] [--world <x> <z>] [--threads <n>]
//        [--json] [--sections <a,b,...>] [--radius <meters>] [--falls <meters>] [--caves]
//        (<x> <z> [<x> <z>]... | --line <x1> <z1> <x2> <z2> <step>)
//        terrain_probe --planet-check <file> [--seed <seed>] [--world <x> <z>] [--json]
//        terrain_probe --list-sections

#include "probe_options.hpp"
#include "probe_record.hpp"
#include "probe_sections.hpp"
#include "probe_world.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

constexpr int kPlanetRefused = 2;

int listSections() {
    for (const probe::Section& s : probe::sections()) {
        std::printf("%-10s %s%s\n", s.name, s.what, s.line ? " (along a line by default)" : "");
    }
    return EXIT_SUCCESS;
}

int planetCheck(const probe::Options& o) {
    const probe::PlanetCheck check = probe::checkPlanet(o);
    const probe::Record record = probe::checkRecord(o, check);
    std::fputs((o.json ? probe::toJson(record) : probe::toText(record)).c_str(), stdout);
    return check.status == "ok" ? EXIT_SUCCESS : kPlanetRefused;
}

// The text of a probe: the document as text, a line as a table.
std::string asText(const probe::Record& document) {
    probe::Record head;
    std::string table;
    for (const probe::Field& f : document.fields()) {
        if (f.key == "line") {
            const auto& line = std::get<probe::Record>(f.value);
            probe::Record about;
            for (const probe::Field& g : line.fields()) {
                if (g.key != "points") {
                    about.put(g);
                }
            }
            head.add("line", std::move(about));
            table = probe::lineTable(line);
        } else {
            head.put(f);
        }
    }
    return probe::toText(head) + table;
}

} // namespace

int main(int argc, char** argv) {
    probe::Options o;
    if (!probe::parseOptions(argc, argv, probe::sectionNames(), o)) {
        std::fputs(probe::usage(argv[0]).c_str(), stderr);
        return EXIT_FAILURE;
    }
    if (o.listSections) {
        return listSections();
    }
    if (!o.planetCheck.empty()) {
        return planetCheck(o);
    }
    try {
        const probe::OpenedPlanet planet = probe::openPlanet(o);
        const probe::ProbeWorld world(o, planet.level);
        const probe::Record document = probe::probeDocument(world, planet.status);
        std::fputs((o.json ? probe::toJson(document) : asText(document)).c_str(), stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "terrain_probe: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
