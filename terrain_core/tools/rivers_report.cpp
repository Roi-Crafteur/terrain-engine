// The lowland river, the figures of the mountains and the freeze of terrain_export_rivers
// (rivers_places.hpp).
#include "rivers_places.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rivers_tool {

namespace {

// A river of kPlainLow to kPlainHigh m^3/s in the plains, meandering.
constexpr float kPlainLow = 300.0f;
constexpr float kPlainHigh = 5000.0f;
constexpr int kPlainTries = 400;

struct Window {
    const char* name;
    double x;
    double z;
    double step;
    int side;
};

// The windows of mountain_freeze_test.
constexpr std::array<Window, 4> kWindows = {{
    {"k2 scene, 8 km at 25 m", 416'857.0, -11'405'966.0, 25.0, 321},
    {"highest wall, 1 km at 4 m", 416'000.0, -11'414'800.0, 4.0, 251},
    {"foot of the fixed chain, 24 km at 100 m", -27'744'000.0, 4'734'000.0, 100.0, 241},
    {"foot of the default chain, 24 km at 100 m", 9'630'245.0, -11'029'895.0, 100.0, 241},
}};

bool sameSample(const terrain::HeightSample& a, const terrain::HeightSample& b) {
    return std::bit_cast<std::uint32_t>(a.height) == std::bit_cast<std::uint32_t>(b.height) &&
           a.water == b.water &&
           std::bit_cast<std::uint32_t>(a.waterLevel) == std::bit_cast<std::uint32_t>(b.waterLevel);
}

struct Freeze {
    long mountains = 0;
    long changed = 0;     // in the corridor of a river or under a lake
    long unexplained = 0; // changed elsewhere: must be 0
};

void count(const terrain::Heightfield& relief, const terrain::Heightfield& field, double x,
           double z, Freeze& f) {
    const terrain::HeightSample a = relief.sampleAt(x, z);
    if (!(a.mountainMask > 0.0f)) {
        return;
    }
    ++f.mountains;
    if (sameSample(a, field.sampleAt(x, z))) {
        return;
    }
    ++f.changed;
    const terrain::Hydrology& h = *field.hydrology();
    const double ground = static_cast<double>(a.height);
    const terrain::RiverCarve c = h.riversAt(x, z, ground);
    if (!c.carved && !c.wet && h.lakeAt(x, z, ground).lake < 0) {
        ++f.unexplained;
    }
}

void addFreeze(Report& r, const char* name, const Freeze& f) {
    r.add("  %s: %ld points under the mountains, %ld changed by the water (%.2f%%), %ld "
          "elsewhere\n",
          name, f.mountains, f.changed,
          f.mountains > 0 ? 100.0 * static_cast<double>(f.changed) / static_cast<double>(f.mountains)
                          : 0.0,
          f.unexplained);
}

} // namespace

bool pickPlain(const terrain::Hydrology& h, Place& view) {
    const std::vector<terrain::RiverSegment>& segments = h.planet().rivers().segments;
    int tries = 0;
    for (std::size_t s = 0; s < segments.size() && tries < kPlainTries; s += 7) {
        const std::vector<terrain::RiverVertex>& v = segments[s].vertices;
        if (v.size() < 3) {
            continue;
        }
        const terrain::RiverVertex& m = v[v.size() / 2];
        if (m.discharge < kPlainLow || m.discharge > kPlainHigh ||
            h.relief().sampleAt(m.x, m.z).mountainMask > 0.0f) {
            continue;
        }
        ++tries;
        for (const std::size_t r : h.reachesNear(m.x, m.z, 10.0)) {
            const terrain::RiverReach reach = h.reach(r);
            const std::size_t mid = reach.points.size() / 2;
            const terrain::RiverPoint& p = reach.points[mid];
            if (reach.segment == static_cast<std::int32_t>(s) && p.meander > 0.5f * p.width) {
                const std::size_t next = std::min(mid + 3, reach.points.size() - 1);
                const double d = 0.5 * p.width + p.meander + 20.0;
                const double dx = reach.points[next].x - p.x;
                const double dz = reach.points[next].z - p.z;
                const double length = std::max(1.0, terrain::dmath::hypot(dx, dz));
                view = placeAt("plain", p.x - d * dz / length, p.z + d * dx / length);
                view.heading = std::fmod(
                    360.0 * terrain::dmath::atan2(dx, -dz) / (2.0 * 3.141592653589793) + 720.0,
                    360.0);
                view.note = "a lowland river of " + std::to_string(static_cast<int>(reach.discharge)) +
                            " m3/s, meanders of " + std::to_string(static_cast<int>(p.meander)) +
                            " m";
                return true;
            }
        }
    }
    return false;
}

void reportMountains(const terrain::Hydrology& h, const terrain::Heightfield& field, double cx,
                     double cz, Report& r) {
    (void)field;
    std::size_t reaches = 0;
    std::size_t over = 0;
    float deepest = 0.0f;
    for (const std::size_t i : h.reachesNear(cx, cz, 128'000.0)) {
        const terrain::RiverReach reach = h.reach(i);
        ++reaches;
        over += reach.deepestCut > 300.0f ? 1u : 0u;
        deepest = std::max(deepest, reach.deepestCut);
    }
    r.add("reaches within 128 km of (%.0f, %.0f) m: %zu, %zu cut more than 300 m, the deepest "
          "%.0f m\n",
          cx, cz, reaches, over, static_cast<double>(deepest));
    const terrain::PlanetLevel& planet = h.planet();
    const terrain::PlanetGrid& g = planet.grid();
    std::size_t lakes = 0;
    std::size_t spilling = 0;
    double area = 0.0;
    for (const terrain::PlanetLake& lake : planet.lakes()) {
        if (lake.playa || h.relief()
                                  .sampleAt(g.centerX(g.column(lake.pit)), g.centerZ(g.row(lake.pit)))
                                  .mountainMask <= 0.0f) {
            continue;
        }
        ++lakes;
        spilling += lake.overflowing ? 1u : 0u;
        area += static_cast<double>(lake.cells) * g.cellArea();
    }
    r.add("lakes of the mountains (their lowest cell under the mask): %zu (%zu spilling), %.0f "
          "km2\n",
          lakes, spilling, area / 1.0e6);
}

void reportFreeze(const terrain::Heightfield& relief, const terrain::Heightfield& field,
                  Report& r) {
    r.add("the windows of mountain_freeze_test:\n");
    Freeze all;
    for (const Window& w : kWindows) {
        Freeze f;
        for (int j = 0; j < w.side; ++j) {
            for (int i = 0; i < w.side; ++i) {
                count(relief, field, w.x + w.step * (i - w.side / 2),
                      w.z + w.step * (j - w.side / 2), f);
            }
        }
        addFreeze(r, w.name, f);
        all.mountains += f.mountains;
        all.changed += f.changed;
        all.unexplained += f.unexplained;
    }
    // The points drawn over the whole world, as the test draws them.
    Freeze drawn;
    std::uint64_t s = 0x9E3779B97F4A7C15ULL;
    const terrain::WorldSize world = relief.world();
    while (drawn.mountains < 20'000) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        const double u = static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        const double v = static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
        count(relief, field, (u - 0.5) * world.x, (v - 0.5) * world.z, drawn);
    }
    addFreeze(r, "points drawn over the world", drawn);
    all.mountains += drawn.mountains;
    all.changed += drawn.changed;
    all.unexplained += drawn.unexplained;
    addFreeze(r, "in all", all);
}

} // namespace rivers_tool
