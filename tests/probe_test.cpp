// terrain_probe (agent 37) on the small world with its water: its values are those of the core bit
// for bit; a line gives exactly the points probed one by one, its end exactly, across the edge of
// the world too; its JSON is well formed and its schema the one frozen below, the same keys for
// every point; the same bytes twice and with the planet level built on 1 or 3 threads; every
// section at sea, on dry land, on a river and at the lip of a fall, with and without the planet.
#include "probe_json_check.hpp"
#include "probe_sections.hpp"
#include "probe_world.hpp"

#include "test_assert.hpp"

#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

using probe::Field;
using probe::Record;

const terrain::WorldSize kSmall{32.0 * 65536.0, 16.0 * 65536.0};

probe::Options smallOptions() {
    probe::Options o;
    o.world = kSmall;
    return o;
}

std::shared_ptr<const terrain::PlanetLevel> smallPlanet(int threads) {
    terrain::PlanetParams params;
    params.threads = threads;
    return std::make_shared<const terrain::PlanetLevel>(
        terrain::kDefaultWorldSeed, terrain::HeightfieldParams{}, params, kSmall);
}

// The field at a path ("relief.ground"), or null.
const Field* at(const Record& r, const std::string& path) {
    const Record* record = &r;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = path.find('.', start);
        const Field* f = record->find(path.substr(start, dot - start));
        if (f == nullptr || dot == std::string::npos) {
            return f;
        }
        record = std::get_if<Record>(&f->value);
        if (record == nullptr) {
            return nullptr;
        }
        start = dot + 1;
    }
}

// The exact number at a path; NaN when absent.
double number(const Record& r, const std::string& path) {
    const Field* f = at(r, path);
    const auto* n = f ? std::get_if<probe::Number>(&f->value) : nullptr;
    return n ? n->value : std::nan("");
}

bool same(double a, float b) {
    return std::bit_cast<std::uint64_t>(a) ==
           std::bit_cast<std::uint64_t>(static_cast<double>(b));
}

struct Places {
    double seaX = 0.0, seaZ = 0.0;
    double dryX = 0.0, dryZ = 0.0;
    double riverX = 0.0, riverZ = 0.0;
    double lipX = 0.0, lipZ = 0.0;
    bool found = false;
};

// A sea point, a dry one, a point of a river course and the lip of a fall of 4 m or more.
Places findPlaces(const probe::ProbeWorld& w) {
    Places p;
    bool sea = false;
    bool dry = false;
    for (double x = -1'000'000.0; x < 1'000'000.0 && !(sea && dry); x += 25'000.0) {
        const terrain::HeightSample s = w.field().sampleAt(x, 0.0);
        if (!sea && s.water == terrain::WaterKind::Ocean) {
            p.seaX = x;
            sea = true;
        }
        if (!dry && s.water == terrain::WaterKind::None) {
            p.dryX = x;
            dry = true;
        }
    }
    bool fall = false;
    for (std::size_t r = 0; r < w.hydrology()->reachCount() && !fall; ++r) {
        const terrain::RiverReach reach = w.hydrology()->reach(r);
        for (const terrain::RiverFall& f : reach.falls) {
            if (!fall && f.top - f.bottom >= 4.0f) {
                p.lipX = terrain::wrapCoordinate(f.x, kSmall.x);
                p.lipZ = terrain::wrapCoordinate(f.z, kSmall.z);
                const terrain::RiverPoint& q = reach.points[reach.points.size() / 2];
                p.riverX = terrain::wrapCoordinate(q.x, kSmall.x);
                p.riverZ = terrain::wrapCoordinate(q.z, kSmall.z);
                fall = true;
            }
        }
    }
    p.found = sea && dry && fall;
    return p;
}

// The values of a point against the core, bit for bit.
void testValues(const probe::ProbeWorld& w, double x, double z, bool river) {
    const Record r = probe::probePoint(w, x, z, probe::chosenSections(w.options(), false));
    const terrain::HeightSample s = w.field().sampleAt(x, z);
    TEST_CHECK(same(number(r, "relief.ground"), w.field().heightAt(x, z)));
    TEST_CHECK(same(number(r, "relief.relief"), w.relief().heightAt(x, z)));
    if (s.water != terrain::WaterKind::None) {
        TEST_CHECK(same(number(r, "water.level"), s.waterLevel));
    } else {
        TEST_CHECK(std::isnan(number(r, "water.level")));
    }
    const terrain::RiverCarve c = w.hydrology()->riversAt(x, z, w.relief().heightAt(x, z));
    TEST_CHECK(c.carved == !std::isnan(number(r, "water.valley.level")));
    if (c.carved) {
        TEST_CHECK(same(number(r, "water.valley.level"), c.level));
    }
    const terrain::PlanetSample p = w.planet()->sampleAt(x, z);
    TEST_CHECK(same(number(r, "planet.temperature"), p.temperature));
    TEST_CHECK(same(number(r, "planet.precipitation"), p.precipitation));
    if (river) {
        const Field* near = at(r, "falls.near");
        TEST_CHECK(near != nullptr && !std::get<std::vector<Record>>(near->value).empty());
    }
    // The column of the rock: on the relief alone its one solid interval ends at the ground of
    // its lattice column, where Marching Cubes places the surface.
    const Field* solid = at(r, "rock.solid");
    const auto& intervals = std::get<std::vector<Record>>(solid->value);
    TEST_CHECK(intervals.size() == 1);
    const auto column = [&](const char* key) {
        return static_cast<double>(std::get<std::int64_t>(at(r, key)->value));
    };
    const double ground = w.field().heightAt(column("rock.columnX"), column("rock.columnZ"));
    TEST_CHECK(intervals.size() == 1 && std::abs(number(intervals[0], "top") - ground) < 1e-3);
}

// A line gives the points probed alone, its end exactly; across the edge of the world too.
void testLine(const probe::ProbeWorld& w, const probe::Line& line, std::size_t count) {
    const auto chosen = probe::chosenSections(w.options(), true);
    const Record r = probe::probeLine(w, line, chosen);
    const auto& points = std::get<std::vector<Record>>(at(r, "points")->value);
    TEST_CHECK(points.size() == count);
    const std::vector<probe::LinePoint> along = probe::linePoints(line);
    TEST_CHECK(along.size() == count && along.back().x == line.x2 && along.back().z == line.z2);
    bool exact = points.size() == along.size();
    for (std::size_t i = 0; exact && i < points.size(); ++i) {
        const Record alone = probe::probePoint(w, along[i].x, along[i].z, chosen);
        Record withDistance;
        withDistance.number("distance", along[i].distance, 2, "m");
        for (const Field& f : alone.fields()) {
            withDistance.put(f);
        }
        const bool wrapped =
            number(points[i], "x") == terrain::wrapCoordinate(along[i].x, kSmall.x);
        const bool stepped = i + 1 == points.size() ||
                             number(points[i], "distance") == static_cast<double>(i) * line.step;
        exact = points[i] == withDistance && wrapped && stepped;
    }
    TEST_CHECK(exact);
}

// The schema of the JSON of the 4 places, frozen: a change of a key is a change of the format of
// terrain_probe (its readers: scripts/verify/probe.py).
const std::vector<std::string>& frozenSchema();

void testDocument(const probe::ProbeWorld& w, const std::string& again) {
    const Record doc = probe::probeDocument(w, "read");
    const std::string json = probe::toJson(doc);
    probe_test::JsonCheck check(json);
    TEST_CHECK(check.wellFormed());
    TEST_CHECK(json == again);
    std::vector<std::string> schema = probe::schemaOf(doc);
    std::sort(schema.begin(), schema.end());
    TEST_CHECK(schema == frozenSchema());
    if (schema != frozenSchema()) {
        for (const std::string& path : schema) {
            std::printf("        \"%s\",\n", path.c_str());
        }
    }
    // The same keys for every point and every section.
    const auto& points = std::get<std::vector<Record>>(at(doc, "points")->value);
    for (const Record& point : points) {
        for (std::size_t i = 0; i < point.fields().size(); ++i) {
            TEST_CHECK(point.fields()[i].key == points.front().fields()[i].key);
            const auto* a = std::get_if<Record>(&point.fields()[i].value);
            const auto* b = std::get_if<Record>(&points.front().fields()[i].value);
            if (a != nullptr && b != nullptr) {
                TEST_CHECK(a->fields().size() == b->fields().size());
            }
        }
    }
    // Every key of the JSON in the text.
    const std::string text = probe::toText(doc);
    for (const auto& [key, value] : probe::flatten(doc)) {
        const std::string leaf = key.substr(key.rfind('.') + 1);
        TEST_CHECK(text.find(leaf + ":") != std::string::npos);
    }
}

std::string documentOf(int threads, const Places& p) {
    probe::Options o = smallOptions();
    o.points = {{p.seaX, p.seaZ}, {p.dryX, p.dryZ}, {p.riverX, p.riverZ}, {p.lipX, p.lipZ}};
    const probe::ProbeWorld w(o, smallPlanet(threads));
    return probe::toJson(probe::probeDocument(w, "read"));
}

// Without the planet: the sections that need it say so, with the same keys.
void testWithoutPlanet(const Places& p) {
    probe::Options o = smallOptions();
    o.points = {{p.dryX, p.dryZ}};
    const probe::ProbeWorld w(o, nullptr);
    const Record r = probe::probePoint(w, p.dryX, p.dryZ, probe::chosenSections(o, false));
    TEST_CHECK(std::get<bool>(at(r, "planet.available")->value) == false);
    TEST_CHECK(std::get<bool>(at(r, "falls.available")->value) == false);
    TEST_CHECK(std::holds_alternative<probe::Absent>(at(r, "water.valley.carved")->value));
    TEST_CHECK(std::get<bool>(at(r, "geography.available")->value));
    const Record doc = probe::probeDocument(w, "none");
    TEST_CHECK(std::get<std::string>(at(doc, "planet.status")->value) == "none");
}

} // namespace

int main() {
    probe::Options o = smallOptions();
    const probe::ProbeWorld w(o, smallPlanet(3));
    const Places p = findPlaces(w);
    TEST_CHECK(p.found);
    if (!p.found) {
        return test::result();
    }
    std::printf("sea (%.0f, %.0f), dry (%.0f, %.0f), river (%.1f, %.1f), lip (%.1f, %.1f)\n",
                p.seaX, p.seaZ, p.dryX, p.dryZ, p.riverX, p.riverZ, p.lipX, p.lipZ);
    testValues(w, p.seaX, p.seaZ, false);
    testValues(w, p.dryX, p.dryZ, false);
    testValues(w, p.riverX, p.riverZ, false);
    testValues(w, p.lipX, p.lipZ, true);
    testLine(w, {p.riverX - 50.0, p.riverZ, p.riverX + 50.0, p.riverZ, 7.0}, 16);
    testLine(w, {p.lipX, p.lipZ - 30.0, p.lipX, p.lipZ + 30.0, 10.0}, 7);
    testLine(w, {0.5 * kSmall.x - 20.0, 1000.0, 0.5 * kSmall.x + 20.0, 1000.0, 10.0}, 5);
    testLine(w, {p.dryX, p.dryZ, p.dryX, p.dryZ, 5.0}, 1);
    probe::Options withPoints = o;
    withPoints.points = {{p.seaX, p.seaZ}, {p.dryX, p.dryZ}, {p.riverX, p.riverZ},
                         {p.lipX, p.lipZ}};
    const probe::ProbeWorld w4(withPoints, smallPlanet(3));
    const std::string one = documentOf(1, p);
    testDocument(w4, one);
    testWithoutPlanet(p);
    return test::result();
}

namespace {

// Sorted: the order of the keys within a record is that of the sections, the order of first
// appearance of the keys of the items of a list depends on the points.
const std::vector<std::string>& frozenSchema() {
    static const std::vector<std::string> schema = [] {
        const std::string paths = R"(
format planet planet.checksum planet.file planet.status points points[].coast
points[].coast.atSea points[].coast.distance points[].coast.mapCoast points[].coast.rugged
points[].coast.seaHeading points[].coast.type points[].falls points[].falls.available
points[].falls.corridor points[].falls.corridor.bottom points[].falls.corridor.distance
points[].falls.corridor.drop points[].falls.corridor.falls points[].falls.corridor.inside
points[].falls.corridor.length points[].falls.corridor.reach points[].falls.corridor.top
points[].falls.near points[].falls.near[].bottom points[].falls.near[].distance
points[].falls.near[].drop points[].falls.near[].footX points[].falls.near[].footZ
points[].falls.near[].lipX points[].falls.near[].lipZ points[].falls.near[].reach
points[].falls.near[].top points[].falls.near[].width points[].falls.radius
points[].geography points[].geography.available points[].geography.belt
points[].geography.character points[].geography.coarse points[].geography.coast
points[].geography.land points[].geography.mapCoast points[].geography.regions
points[].geography.relief points[].geography.rift points[].geography.riftFloor
points[].geography.rugged points[].geography.sea points[].planet points[].planet.available
points[].planet.cell points[].planet.cellElevation points[].planet.centerX
points[].planet.centerZ points[].planet.column points[].planet.discharge
points[].planet.elevation points[].planet.frozen points[].planet.humidity
points[].planet.lake points[].planet.lakeLevel points[].planet.lakePlaya
points[].planet.ocean points[].planet.pet points[].planet.precipitation
points[].planet.river points[].planet.row points[].planet.runoff points[].planet.strahler
points[].planet.temperature points[].planet.waterSurface points[].planet.windHeading
points[].relief points[].relief.base points[].relief.continentalness points[].relief.detail
points[].relief.downhill points[].relief.ground points[].relief.intensity
points[].relief.mountain points[].relief.mountainMask points[].relief.relief
points[].relief.slope points[].render points[].render.horizon points[].render.horizon.ground
points[].render.horizon.kind points[].render.horizon.level points[].render.horizon.x
points[].render.horizon.z points[].render.radius points[].render.tiles
points[].render.tiles[].chunkX points[].render.tiles[].chunkZ points[].render.tiles[].high
points[].render.tiles[].lod points[].render.tiles[].low points[].render.tiles[].vertices
points[].render.tiles[].wet points[].rock points[].rock.columnX points[].rock.columnZ
points[].rock.densityAtGround points[].rock.faces points[].rock.scanFrom
points[].rock.scanTo points[].rock.solid points[].rock.solid[].bottom
points[].rock.solid[].top points[].rock.steepest points[].surface
points[].surface.baseTemperature points[].surface.biome points[].surface.biomes
points[].surface.biomes[].name points[].surface.biomes[].weight points[].surface.exposure
points[].surface.humidity points[].surface.material points[].surface.materials
points[].surface.materials[].name points[].surface.materials[].weight
points[].surface.plants points[].surface.plants.boulder points[].surface.plants.bush
points[].surface.plants.cactus points[].surface.plants.flower
points[].surface.plants.seaweed points[].surface.plants.tallGrass
points[].surface.plants.tree points[].surface.slope points[].surface.temperature
points[].water points[].water.depth points[].water.flow points[].water.flow.heading
points[].water.flow.x points[].water.flow.z points[].water.hydrology points[].water.kind
points[].water.lake points[].water.lake.id points[].water.lake.kind
points[].water.lake.level points[].water.level points[].water.valley
points[].water.valley.bank points[].water.valley.carved points[].water.valley.floor
points[].water.valley.gorge points[].water.valley.level points[].water.valley.mountain
points[].water.valley.pond points[].water.valley.reach points[].water.valley.surface
points[].water.valley.wet points[].x points[].z tool world world.generatorVersion world.seed
world.x world.z)";
        std::vector<std::string> out;
        std::size_t start = paths.find_first_not_of(" \n");
        while (start != std::string::npos) {
            const std::size_t end = paths.find_first_of(" \n", start);
            out.push_back(paths.substr(start, end - start));
            start = paths.find_first_not_of(" \n", end);
        }
        return out;
    }();
    return schema;
}

} // namespace
