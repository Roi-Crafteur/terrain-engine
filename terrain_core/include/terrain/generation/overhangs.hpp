#pragma once

#include "terrain/generation/cliffs.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <functional>

namespace terrain {

// Lengths are in meters. Overhangs bend the walls of the strata (see CliffColumn): the hard
// bank at the top of a stratum juts out by capOverhang, the rock under it recedes by
// undercut. Where they apply is decided column by column, deterministically.
struct OverhangParams {
    // Control mask = patches * cliffs * steepness * river fade, in [0, 1]:
    // patches: smoothstep(patchLow, patchHigh, fbm) with 3 octaves;
    double patchWavelength = 600.0;
    float patchLow = -0.2f;
    float patchHigh = 0.1f;
    // cliffs: smoothstep(minCliff, 1, M), as the blend with the relief where M < 1 would fill
    // the notches;
    float minCliff = 0.85f;
    // steepness: smoothstep(minSlope, fullSlope, |grad H|): thin knobs of a stratum grow on
    // gentle ground;
    float minSlope = 0.15f;
    float fullSlope = 0.3f;
    // river fade: smoothstep(riverFadeStart * riverReach, riverReach, river distance), so the
    // banks of the rivers, the gorges and the cirques keep their walls.
    float riverFadeStart = 0.5f;

    float capOverhang = 4.0f;
    float undercut = 8.0f;

    // Zone: full weight where a footprint f_k is within zoneMargin of [-undercut,
    // capOverhang], the only place where the overhangs change rock into air or air into rock,
    // and none beyond twice zoneMargin. Strata are at least 20 m apart in footprint, so a
    // column is in the zone of one stratum at most.
    float zoneMargin = 2.0f;
    // Support: a stratum only overhangs near a point of it deeper than undercut +
    // supportMargin (full weight at undercut + 2 * supportMargin), searched by probes in 8
    // directions at each probe radius. A stratum with no such point (a thin knob) keeps its
    // walls, so its bank never floats.
    float supportMargin = 2.0f;
    double innerProbeRadius = 12.0;
    double outerProbeRadius = 24.0;
};

// Height of the relief at (x, z), read by the support probes.
using ReliefHeight = std::function<float(double x, double z)>;

class OverhangField {
public:
    // Periodic on the world (WorldSize). Throws std::invalid_argument for an invalid world.
    explicit OverhangField(std::uint64_t seed, const OverhangParams& params = {},
                           const WorldSize& world = {});

    // Patch factor of the control mask at (x, z), in [0, 1].
    [[nodiscard]] float patchAt(double x, double z) const;
    // Control mask of a cliff column at (x, z): 0 where the cliffs are off.
    [[nodiscard]] float controlAt(double x, double z, const CliffColumn& column,
                                  const CliffParams& cliffs) const;

    // Zone weight of a footprint, in [0, 1].
    [[nodiscard]] float zoneWeight(float footprint) const;
    // Support weight of a stratum whose deepest probe is depth meters inside it, in [0, 1].
    [[nodiscard]] float supportWeight(float depth) const;
    // Deepest point of stratum k near the column (x, z), probing the relief: the largest
    // -f_k over the column itself and its probes, each probe with the slope and rim
    // recession of the column.
    [[nodiscard]] float supportDepth(const CliffField& cliffs, const CliffColumn& column, int k,
                                     double x, double z, const ReliefHeight& relief) const;

    // Overhang weight of a column, in [0, 1]: control * max_k zone_k * support_k over the
    // strata of the column. Continuous in (x, z); 0 away from the walls.
    [[nodiscard]] float weightAt(const CliffField& cliffs, const CliffColumn& column, double x,
                                 double z, float control, const ReliefHeight& relief) const;

    // Sets the overhangs of a column from its weight.
    void apply(CliffColumn& column, float weight) const;

    [[nodiscard]] const OverhangParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }

private:
    OverhangParams params_;
    WorldSize world_;
    std::uint64_t patchSeed_;
};

} // namespace terrain
