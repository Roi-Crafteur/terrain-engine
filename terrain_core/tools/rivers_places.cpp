// The options, the report and the places of terrain_export_rivers (rivers_places.hpp).
#include "rivers_places.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rivers_tool {

namespace {

constexpr double kSearch = 128'000.0; // meters around the center for the places
constexpr std::size_t kNone = static_cast<std::size_t>(-1);

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

bool fail(const char* what, const char* text) {
    std::fprintf(stderr, "Invalid %s '%s'\n", what, text);
    return false;
}

bool parseOption(int argc, char** argv, int& i, Options& o) {
    const std::string flag = argv[i];
    const auto need = [&](int count) { return i + count < argc; };
    bool ok = true;
    if (flag == "--planet" && need(1)) {
        o.planet = argv[++i];
    } else if (flag == "--seed" && need(1)) {
        ok = parse(argv[++i], o.seed) || fail("seed", argv[i]);
    } else if (flag == "--world" && need(2)) {
        ok = (parse(argv[i + 1], o.world.x) && parse(argv[i + 2], o.world.z)) ||
             fail("world", argv[i + 1]);
        i += 2;
    } else if (flag == "--threads" && need(1)) {
        ok = (parse(argv[++i], o.threads) && o.threads >= 0) || fail("threads", argv[i]);
    } else if (flag == "--width" && need(1)) {
        ok = (parse(argv[++i], o.width) && o.width >= 16) || fail("width", argv[i]);
    } else if (flag == "--pixels" && need(1)) {
        ok = (parse(argv[++i], o.pixels) && o.pixels >= 16) || fail("pixels", argv[i]);
    } else if (flag == "--map" && need(3)) {
        Place p = placeAt(argv[i + 1], 0.0, 0.0);
        ok = (parse(argv[i + 2], p.x) && parse(argv[i + 3], p.z)) || fail("map", argv[i + 1]);
        o.maps.push_back(p);
        i += 3;
    } else if (flag == "--view" && need(5)) {
        Place p = placeAt(argv[i + 1], 0.0, 0.0);
        ok = (parse(argv[i + 2], p.x) && parse(argv[i + 3], p.z) &&
              parse(argv[i + 4], p.heading) && parse(argv[i + 5], p.above)) ||
             fail("view", argv[i + 1]);
        o.views.push_back(p);
        i += 5;
    } else {
        ok = fail("option", argv[i]);
    }
    return ok;
}

// Degrees clockwise from -Z of the way from a to b.
double headingOf(double ax, double az, double bx, double bz) {
    const double turns = terrain::dmath::atan2(bx - ax, -(bz - az)) / (2.0 * 3.141592653589793);
    return std::fmod(360.0 * turns + 720.0, 360.0);
}

// The point d meters beside the axis of a reach at its point i (left of the course), and the
// heading down the course there (or up it with upstream).
Place besideCourse(const terrain::RiverReach& reach, std::size_t i, double d, bool upstream,
                   const std::string& name) {
    const std::size_t n = reach.points.size();
    const std::size_t first = i + 3 < n ? i : (i >= 3 ? i - 3 : 0);
    const terrain::RiverPoint& a = reach.points[first];
    const terrain::RiverPoint& b = reach.points[std::min(first + 3, n - 1)];
    const double length = std::max(1.0, terrain::dmath::hypot(b.x - a.x, b.z - a.z));
    const terrain::RiverPoint& p = reach.points[i];
    Place place = placeAt(name, p.x - d * (b.z - a.z) / length, p.z + d * (b.x - a.x) / length);
    place.heading = headingOf(a.x, a.z, b.x, b.z);
    if (upstream) {
        place.heading = std::fmod(place.heading + 180.0, 360.0);
    }
    return place;
}

struct Pick {
    std::size_t reach = kNone;
    std::size_t point = 0;
    double score = -std::numeric_limits<double>::infinity();
};

// The valley: the largest river under the mountains in an open valley (no gorge); the gorge: the
// largest one in a gorge 100 to 300 m deep; the fall: the highest.
void pickReaches(const terrain::Hydrology& h, double cx, double cz, Pick& valley, Pick& gorge,
                 Pick& fall) {
    for (const std::size_t r : h.reachesNear(cx, cz, kSearch)) {
        const terrain::RiverReach reach = h.reach(r);
        const std::size_t mid = reach.points.size() / 2;
        const terrain::RiverPoint& m = reach.points[mid];
        if (m.mountain >= 0.9f && m.gorge <= 0.05f && reach.discharge > valley.score) {
            valley = {r, mid, reach.discharge};
        }
        for (std::size_t i = 1; i + 1 < reach.points.size(); ++i) {
            const terrain::RiverPoint& p = reach.points[i];
            if (p.gorge >= 0.95f && p.mountain >= 0.5f && reach.deepestCut >= 100.0f &&
                reach.deepestCut <= 300.0f && reach.discharge > gorge.score) {
                gorge = {r, i, reach.discharge};
            }
        }
        for (const terrain::RiverFall& f : reach.falls) {
            if (f.top - f.bottom > fall.score) {
                fall = {r, 0, f.top - f.bottom};
                for (std::size_t i = 0; i < reach.points.size(); ++i) {
                    if (reach.points[i].level <= f.bottom) {
                        fall.point = i;
                        break;
                    }
                }
            }
        }
    }
}

// The largest lake of the mountains near (cx, cz) that the relief holds, and a point of its shore
// facing its lowest cell.
bool pickLake(const terrain::Hydrology& h, const terrain::Heightfield& field, double cx,
              double cz, Place& view) {
    const terrain::PlanetLevel& planet = h.planet();
    const terrain::PlanetGrid& g = planet.grid();
    std::vector<std::pair<std::uint32_t, std::int32_t>> lakes; // cells, lake
    for (std::size_t l = 0; l < planet.lakes().size(); ++l) {
        const terrain::PlanetLake& lake = planet.lakes()[l];
        const double x = g.centerX(g.column(lake.pit));
        const double z = g.centerZ(g.row(lake.pit));
        if (!lake.playa && terrain::dmath::hypot(x - cx, z - cz) <= kSearch &&
            h.relief().sampleAt(x, z).mountainMask > 0.0f) {
            lakes.emplace_back(lake.cells, static_cast<std::int32_t>(l));
        }
    }
    std::sort(lakes.begin(), lakes.end(), [](const auto& a, const auto& b) {
        return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    for (const auto& [cells, best] : lakes) {
        const float level = h.lakeLevel(best);
        if (!std::isfinite(level)) {
            continue;
        }
        const terrain::PlanetLake& lake = planet.lakes()[static_cast<std::size_t>(best)];
        const double px = g.centerX(g.column(lake.pit));
        const double pz = g.centerZ(g.row(lake.pit));
        double nearest = std::numeric_limits<double>::infinity();
        for (int k = 0; k < 16; ++k) {
            const double dx = terrain::dmath::cosTurns(k / 16.0);
            const double dz = terrain::dmath::sinTurns(k / 16.0);
            for (double d = 100.0; d < std::min(nearest, 30'000.0); d += 100.0) {
                const terrain::HeightSample s = field.sampleAt(px + d * dx, pz + d * dz);
                if (s.water == terrain::WaterKind::None && s.height > level + 2.0f) {
                    nearest = d;
                    view = placeAt("lake", px + d * dx, pz + d * dz);
                    view.heading = headingOf(view.x, view.z, px, pz);
                    break;
                }
            }
        }
        char note[200];
        std::snprintf(note, sizeof note,
                      "lake %d of a high basin, level %.0f m over the relief (%.0f m on the "
                      "planet level), %u cells of 8 km",
                      best, static_cast<double>(level), static_cast<double>(lake.level), cells);
        view.note = note;
        return std::isfinite(nearest);
    }
    return false;
}

} // namespace

bool parseOptions(int argc, char** argv, Options& o) {
    if (argc < 2 || argv[1][0] == '-') {
        return false;
    }
    o.directory = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (!parseOption(argc, argv, i, o)) {
            return false;
        }
    }
    try {
        terrain::validate(o.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", "(multiples of 65,536 m expected)");
    }
    return true;
}

void Report::add(const char* format, ...) {
    std::array<char, 1024> line{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(line.data(), line.size(), format, args);
    va_end(args);
    std::fputs(line.data(), stdout);
    text_ += line.data();
}

bool Report::write(const std::filesystem::path& path) const {
    std::ofstream out(path);
    out << text_;
    return static_cast<bool>(out);
}

void addPlaces(const terrain::Hydrology& h, const terrain::Heightfield& field, double rx,
               double rz, double lx, double lz, Options& o, Report& r) {
    Pick valley;
    Pick gorge;
    Pick fall;
    pickReaches(h, rx, rz, valley, gorge, fall);
    if (valley.reach != kNone) {
        const terrain::RiverReach reach = h.reach(valley.reach);
        const terrain::RiverPoint& p = reach.points[valley.point];
        Place v = besideCourse(reach, valley.point, 0.5 * p.width + 30.0, false, "valley");
        v.tilt = 0.06;
        v.note = "a river of " + std::to_string(static_cast<int>(reach.discharge)) +
                 " m3/s at the bottom of its valley, a floor " +
                 std::to_string(static_cast<int>(p.floorWidth)) + " m wide";
        o.views.push_back(v);
    }
    if (gorge.reach != kNone) {
        const terrain::RiverReach reach = h.reach(gorge.reach);
        const terrain::RiverPoint& p = reach.points[gorge.point];
        Place v = besideCourse(reach, gorge.point, 0.5 * p.width + 4.0, false, "gorge");
        v.tilt = 0.15;
        v.note = "a gorge through a crest, cut up to " +
                 std::to_string(static_cast<int>(reach.deepestCut)) + " m";
        o.views.push_back(v);
    }
    if (fall.reach != kNone) {
        const terrain::RiverReach reach = h.reach(fall.reach);
        const std::size_t i = std::min(fall.point + 4, reach.points.size() - 1);
        Place v = besideCourse(reach, i, 0.5 * reach.points[i].width + 10.0, true, "fall");
        v.tilt = 0.08;
        v.note = "a fall of " + std::to_string(static_cast<int>(fall.score)) + " m";
        o.views.push_back(v);
    }
    Place lake;
    if (pickLake(h, field, lx, lz, lake)) {
        lake.tilt = -0.01;
        o.views.push_back(lake);
    }
    Place plain;
    if (pickPlain(h, plain)) {
        plain.tilt = -0.01;
        o.views.push_back(plain);
    }
    r.add("places: around (%.0f, %.0f) m valley %s, gorge %s, fall %s; around (%.0f, %.0f) m "
          "lake %s; lowland river %s\n",
          rx, rz, valley.reach != kNone ? "found" : "none", gorge.reach != kNone ? "found" : "none",
          fall.reach != kNone ? "found" : "none", lx, lz, lake.name.empty() ? "none" : "found",
          plain.name.empty() ? "none" : "found");
}

} // namespace rivers_tool
