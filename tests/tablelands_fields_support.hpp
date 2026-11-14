#pragma once

// The fields of the plateaus of the default world switched on (agent 46, tableland_relief.hpp), as
// the Heightfield reads them (heightfield_tablelands.cpp), for the tests of their shapes and of their
// patterns; and the edges of the provinces, from a lattice of 32 km.

#include "tablelands_support.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/tablelands.hpp"

#include "tableland_relief.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace tablelands_fields {

using terrain::tableland_relief::Fields;
using terrain::tableland_relief::Province;

// The seeds of the Heightfield (heightfield.cpp: the layer kTablelands, the stream
// kTablelandRelief).
inline constexpr std::uint64_t kTablelandsLayer = 21;
inline constexpr std::uint64_t kReliefStream = 1001;

inline const terrain::Heightfield& field() {
    return tablelands_test::field();
}

inline double wrapX(double x) {
    return terrain::noise::wrapCanonical(x, field().world().x);
}
inline double wrapZ(double z) {
    return terrain::noise::wrapCanonical(z, field().world().z);
}

inline const terrain::tableland_relief::Noises& noises() {
    static const terrain::tableland_relief::Noises n = terrain::tableland_relief::makeNoises(
        terrain::noise::deriveSeed(
            terrain::noise::deriveSeed(terrain::kDefaultWorldSeed, kTablelandsLayer),
            kReliefStream),
        field().world());
    return n;
}

// The provinces at (x, z) as the Heightfield reads them.
inline Province provinceAt(double x, double z) {
    const terrain::Tablelands& t = *field().tablelands();
    const double cx = wrapX(x);
    const double cz = wrapZ(z);
    const terrain::GeographySample g = field().geography()->sampleAt(cx, cz);
    const double step = terrain::tableland_relief::kShareStep;
    const double ax = wrapX(cx + step);
    const double az = wrapZ(cz + step);
    const terrain::Geography& geo = *field().geography();
    return {t.at(cx, cz, g), t.tablelandShare(ax, cz, geo.sampleAt(ax, cz).coast),
            t.tablelandShare(cx, az, geo.sampleAt(cx, az).coast)};
}

inline Fields fieldsAt(double x, double z) {
    return terrain::tableland_relief::fieldsAt(noises(), wrapX(x), wrapZ(z), provinceAt(x, z));
}

inline double heightAt(double x, double z) {
    return static_cast<double>(field().sampleAt(x, z).height);
}

// The points of a lattice of kSiteStep meters: on the edges of the dry plateaus (the share of their
// escarpments), on their coastal edges (within the fade of the coast, 60 to 150 km of it), at the
// far margin of the provinces (a share under 0.1) and in the hearts of the thickest plateaus.
inline constexpr double kSiteStep = 32'768.0;
struct Sites {
    std::vector<std::array<double, 2>> edges;
    std::vector<std::array<double, 2>> coastal;
    std::vector<std::array<double, 2>> margins;
    std::vector<std::array<double, 2>> thick;
    std::size_t checked = 0; // points where tablelandShare was checked against at
    std::size_t wrong = 0;
};

inline Sites findSites() {
    Sites s;
    const terrain::WorldSize& w = field().world();
    for (double z = -0.5 * w.z + 0.5 * kSiteStep; z < 0.5 * w.z; z += kSiteStep) {
        for (double x = -0.5 * w.x + 0.5 * kSiteStep; x < 0.5 * w.x; x += kSiteStep) {
            const terrain::GeographySample g = field().geography()->sampleAt(x, z);
            const terrain::TablelandPoint p = field().tablelands()->at(x, z, g);
            if (!(p.tableland > 0.0f)) {
                continue;
            }
            ++s.checked;
            s.wrong += field().tablelands()->tablelandShare(x, z, g.coast) != p.tableland;
            const bool edge = p.tableland > 0.4f && p.tableland < 0.8f;
            if (edge && p.arid > 0.6f) {
                s.edges.push_back({x, z});
            }
            if (edge && g.coast < 150'000.0) {
                s.coastal.push_back({x, z});
            }
            if (p.tableland < 0.1f) {
                s.margins.push_back({x, z});
            }
            if (p.tableland > 0.99f && fieldsAt(x, z).thickness > 1450.0) {
                s.thick.push_back({x, z});
            }
        }
    }
    return s;
}

} // namespace tablelands_fields
