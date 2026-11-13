// The sites of the lows of agent 52 (low_sites.hpp).
#include "low_sites.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/noise.hpp"

#include <array>

namespace terrain::lows {

namespace {

// The streams of the seeds of the families: new ones go at the end.
enum Stream : std::uint64_t { kGrabens, kBasins };

constexpr std::array<const char*, kLowKindCount> kNames = {"depression", "basin", "graben"};

} // namespace

Sites makeSites(std::uint64_t seed, const LowsParams& params, const Geography& geography,
                const std::vector<depressions::Site>& depressions, const Exclusion& excluded) {
    Sites out;
    if (!params.enabled) {
        return out;
    }
    if (params.grabens) {
        out.grabens =
            makeGrabens(noise::deriveSeed(seed, kGrabens), geography, params.density, excluded);
    }
    if (params.basins) {
        out.basins = makeBasins(noise::deriveSeed(seed, kBasins), geography, params.density,
                                depressions, excluded);
    }
    return out;
}

bool mayReach(const Sites& sites, double x, double z) {
    return (sites.grabens && !sites.grabens->reach.at(x, z).empty()) ||
           (sites.basins && !sites.basins->reach.at(x, z).empty());
}

std::vector<LowSite> listOf(const Sites& sites) {
    std::vector<LowSite> out;
    if (sites.grabens) {
        for (const Graben& g : sites.grabens->sites) {
            const double axis = dmath::atan2(g.axisZ, g.axisX) / (2.0 * 3.14159265358979323846);
            out.push_back({LowKind::Graben, g.x, g.z, g.length, g.reach,
                           static_cast<float>(g.floor), axis < 0.0 ? axis + 1.0 : axis});
        }
    }
    if (sites.basins) {
        for (const depressions::Site& s : sites.basins->sites) {
            out.push_back({LowKind::Basin, s.x, s.z, s.radius, basinReach(s),
                           static_cast<float>(s.floor), s.angle});
        }
    }
    return out;
}

} // namespace terrain::lows

namespace terrain {

const char* lowKindName(LowKind kind) {
    return lows::kNames[static_cast<std::size_t>(kind)];
}

void lowsOn(HeightfieldParams& relief, PlanetParams& /*planet*/) {
    relief.lows.enabled = true;
}

} // namespace terrain
