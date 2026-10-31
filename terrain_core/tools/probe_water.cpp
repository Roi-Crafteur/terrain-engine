// The sections water and render of terrain_probe (probe_sections.hpp): the water of the world at a
// point as the core decides it, and what it hands to the renderers there (the vertices of the
// tiles of LOD 0 to 3, the sample of the horizon level 0). The water of the tool
// terrain_probe_water before it, which terrain_probe replaces.
#include "probe_sections.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace probe {

namespace {

constexpr double kHorizonPitch = 8.0; // meters between the samples of the horizon level 0

terrain::TileKey tileAt(const ProbeWorld& w, const Point& p, int lod) {
    const terrain::ChunkCoord chunk =
        terrain::wrapChunk(terrain::worldToChunk(p.x, p.z), w.world());
    return terrain::tileContaining(chunk, lod);
}

void level(Record& r, const char* key, bool present, double value) {
    if (present) {
        r.number(key, value, 3, "m");
    } else {
        r.none(key);
    }
}

Record valleyOf(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::Hydrology* h = w.hydrology();
    if (h == nullptr) {
        for (const char* key : {"carved", "wet", "level", "floor", "surface", "bank", "pond",
                                "gorge", "mountain", "reach"}) {
            r.none(key);
        }
        return r;
    }
    const terrain::RiverCarve c = h->riversAt(p.x, p.z, p.relief);
    r.flag("carved", c.carved).flag("wet", c.wet);
    level(r, "level", c.carved, c.level);
    level(r, "floor", c.carved, c.floor);
    level(r, "surface", c.carved, c.height);
    level(r, "bank", std::isfinite(c.bank), c.bank);
    level(r, "pond", std::isfinite(c.pond), c.pond);
    r.number("gorge", c.gorge, 3).number("mountain", c.mountain, 3);
    if (c.reach == static_cast<std::size_t>(-1)) {
        r.none("reach");
    } else {
        r.integer("reach", static_cast<std::int64_t>(c.reach));
    }
    return r;
}

Record lakeOf(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::Hydrology* h = w.hydrology();
    const terrain::LakeWater lake = h != nullptr ? h->lakeAt(p.x, p.z, p.relief)
                                                 : terrain::LakeWater{};
    if (lake.lake < 0) {
        return r.none("id").none("kind").none("level");
    }
    return r.integer("id", lake.lake)
        .text("kind", kindName(lake.kind))
        .number("level", lake.level, 3, "m");
}

// The current as the tiles give it to Unreal: the column of the LOD 0 lattice nearest to the
// point (TileData::waterColumns, empty without a river or a lake in the tile).
Record flowOf(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::TileData& t = w.tile(tileAt(w, p, 0));
    const double cell = terrain::tileCellMeters(0);
    const auto index = [&](double v, double origin, double period) {
        const double offset = terrain::nearestImage(v, origin, period) - origin;
        return std::clamp(static_cast<int>(std::lround(offset / cell)), 0,
                          terrain::kGridSamples - 1);
    };
    const int i = index(p.x, t.originX, w.world().x);
    const int k = index(p.z, t.originZ, w.world().z);
    const std::size_t at = static_cast<std::size_t>(k * terrain::kGridSamples + i);
    const terrain::WaterColumn c =
        at < t.waterColumns.size() ? t.waterColumns[at] : terrain::WaterColumn{};
    if (c.flowX == 0.0f && c.flowZ == 0.0f) {
        return r.none("x").none("z").none("heading");
    }
    return r.number("x", c.flowX, 4)
        .number("z", c.flowZ, 4)
        .number("heading", headingOf(c.flowX, c.flowZ), 1, "deg");
}

// The vertices of the tile of a LOD holding the point within radius of it (horizontally).
Record tileOf(const ProbeWorld& w, const Point& p, int lod) {
    const terrain::TileKey key = tileAt(w, p, lod);
    const terrain::TileData& data = w.tile(key);
    const double px = terrain::nearestImage(p.x, data.originX, w.world().x);
    const double pz = terrain::nearestImage(p.z, data.originZ, w.world().z);
    const double radius = w.options().radius;
    int near = 0;
    int wet = 0;
    float low = std::numeric_limits<float>::infinity();
    float high = -std::numeric_limits<float>::infinity();
    for (std::size_t v = 0; v < data.mesh.vertices.size(); ++v) {
        const terrain::Vec3f& q = data.mesh.vertices[v];
        const double dx = data.originX + static_cast<double>(q.x) - px;
        const double dz = data.originZ + static_cast<double>(q.z) - pz;
        if (dx * dx + dz * dz > radius * radius) {
            continue;
        }
        ++near;
        const float l = data.waterLevels[v];
        if (l > terrain::kNoWaterLevel) {
            ++wet;
            low = std::min(low, l);
            high = std::max(high, l);
        }
    }
    Record r;
    r.integer("lod", lod)
        .integer("chunkX", key.origin.x)
        .integer("chunkZ", key.origin.z)
        .integer("vertices", near)
        .integer("wet", wet);
    level(r, "low", wet > 0, low);
    level(r, "high", wet > 0, high);
    return r;
}

} // namespace

Record waterSection(const ProbeWorld& w, const Point& p) {
    const terrain::HeightSample& s = p.sample;
    const bool wet = s.water != terrain::WaterKind::None;
    Record r;
    r.text("kind", kindName(s.water));
    level(r, "level", wet, s.waterLevel);
    level(r, "depth", wet, static_cast<double>(s.waterLevel) - static_cast<double>(s.height));
    return r.flag("hydrology", w.hydrology() != nullptr)
        .add("valley", valleyOf(w, p))
        .add("lake", lakeOf(w, p))
        .add("flow", flowOf(w, p));
}

Record renderSection(const ProbeWorld& w, const Point& p) {
    std::vector<Record> tiles;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        tiles.push_back(tileOf(w, p, lod));
    }
    const double hx = kHorizonPitch * std::round(p.x / kHorizonPitch);
    const double hz = kHorizonPitch * std::round(p.z / kHorizonPitch);
    const terrain::HeightSample s = w.field().sampleAt(hx, hz);
    const bool wet = s.water != terrain::WaterKind::None;
    Record horizon;
    horizon.number("x", hx, 0, "m").number("z", hz, 0, "m").text("kind", kindName(s.water));
    level(horizon, "level", wet, s.waterLevel);
    horizon.number("ground", s.height, 3, "m");
    Record r;
    r.number("radius", w.options().radius, 1, "m").list("tiles", std::move(tiles));
    return r.add("horizon", std::move(horizon));
}

} // namespace probe
