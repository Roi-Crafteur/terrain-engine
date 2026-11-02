// The sections relief, geography and coast of terrain_probe (probe_sections.hpp).
#include "probe_sections.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/geography.hpp"

#include <optional>

namespace probe {

namespace {

constexpr double kSlopeStep = 1.0;  // meters: the central differences of the slope (LOD 0)
constexpr double kCoastStep = 32.0; // meters: the gradient of the coast signal

struct Gradient {
    double x = 0.0;
    double z = 0.0;
};

Gradient groundGradient(const ProbeWorld& w, const Point& p) {
    const terrain::Heightfield& f = w.field();
    const auto h = [&](double dx, double dz) {
        return static_cast<double>(f.heightAt(wrapX(w, p.x + dx), wrapZ(w, p.z + dz)));
    };
    return {(h(kSlopeStep, 0.0) - h(-kSlopeStep, 0.0)) / (2.0 * kSlopeStep),
            (h(0.0, kSlopeStep) - h(0.0, -kSlopeStep)) / (2.0 * kSlopeStep)};
}

std::optional<terrain::GeographySample> geographyAt(const ProbeWorld& w, double x, double z) {
    const terrain::Geography* g = w.field().geography();
    if (g == nullptr) {
        return std::nullopt;
    }
    return g->sampleAt(wrapX(w, x), wrapZ(w, z));
}

} // namespace

Record reliefSection(const ProbeWorld& w, const Point& p) {
    const Gradient g = groundGradient(w, p);
    const double rise = terrain::dmath::hypot(g.x, g.z);
    const terrain::HeightSample& s = p.sample;
    Record r;
    r.number("relief", p.relief, 3, "m")
        .number("ground", p.ground, 3, "m")
        .number("slope", slopeDegrees(rise), 2, "deg");
    if (rise > 0.0) {
        r.number("downhill", headingOf(-g.x, -g.z), 1, "deg");
    } else {
        r.none("downhill");
    }
    r.number("continentalness", s.continentalness, 4)
        .number("mountainMask", s.mountainMask, 4)
        .number("intensity", s.relief, 4)
        .number("base", s.base, 2, "m")
        .number("mountain", s.mountain, 2, "m")
        .number("detail", s.detail, 2, "m");
    return r;
}

Record geographySection(const ProbeWorld& w, const Point& p) {
    const std::optional<terrain::GeographySample> g = geographyAt(w, p.x, p.z);
    Record r;
    r.flag("available", g.has_value());
    const auto put = [&](const char* key, double value, int decimals, const char* unit) {
        if (g) {
            r.number(key, value, decimals, unit);
        } else {
            r.none(key);
        }
    };
    const terrain::GeographySample s = g.value_or(terrain::GeographySample{});
    put("coast", s.coast, 1, "m");
    put("mapCoast", s.mapCoast, 1, "m");
    put("land", s.land, 2, "m");
    put("sea", s.sea, 2, "m");
    put("belt", s.belt, 4, "");
    put("relief", s.relief, 4, "");
    put("character", s.character, 4, "");
    put("regions", s.regions, 4, "");
    put("rugged", s.rugged, 4, "");
    put("coarse", s.coarse, 4, "");
    put("rift", s.rift, 4, "");
    put("riftFloor", s.riftFloor, 2, "m");
    return r;
}

// The coast types (cliffs, beaches, fjords...) come with the coastal relief (agent 33): "type"
// stays absent until then.
Record coastSection(const ProbeWorld& w, const Point& p) {
    const std::optional<terrain::GeographySample> g = geographyAt(w, p.x, p.z);
    Record r;
    r.flag("atSea", p.sample.water == terrain::WaterKind::Ocean);
    if (!g) {
        return r.none("distance").none("seaHeading").none("mapCoast").none("rugged").none("type");
    }
    const auto coast = [&](double dx, double dz) {
        return geographyAt(w, p.x + dx, p.z + dz)->coast;
    };
    const double gx = coast(kCoastStep, 0.0) - coast(-kCoastStep, 0.0);
    const double gz = coast(0.0, kCoastStep) - coast(0.0, -kCoastStep);
    r.number("distance", g->coast, 1, "m");
    if (gx != 0.0 || gz != 0.0) {
        r.number("seaHeading", headingOf(-gx, -gz), 1, "deg"); // down the signal: the sea
    } else {
        r.none("seaHeading");
    }
    return r.number("mapCoast", g->mapCoast, 1, "m").number("rugged", g->rugged, 4).none("type");
}

} // namespace probe
