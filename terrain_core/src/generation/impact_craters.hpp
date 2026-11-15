#pragma once

// The impact craters of agent 52, internal to terrain_core (their forms: crater_forms.hpp). Rare,
// as on the Earth (about 200 known on its land): in three classes by diameter, 0.4 to 2 km, 2 to
// 10 km and 10 to 60 km, about 200, 80 and 15 of them on the land of the world of the game (in
// proportion to the area on another world, times the density), the diameters of a class along
// the power law of the impacts (many more small ones). Placed by the seed alone, the largest
// first, anywhere on the plains: accepted only where their whole footprint (low_footprint.hpp)
// lies on land without relief (intensity at most 0.11: no mountain, they begin at 0.12 and keep
// their bits), out of the belts, 40 km from the sea at least, out of the excluded zone (agent 46)
// and of the reach of every other low; never in a cluster (their reaches kept 20 km apart).
// Most of them worn: their wear grows with their age (most of them old), the humidity of their
// climate (the pre-climate of the geography, pre_climate.hpp: the fresh ones lie in the dry
// lands, as Meteor Crater and Tenoumer) and the softness of their rock (the worn character of
// the geography). Pure arithmetic of doubles, noises and hashes: the same bits on every platform,
// periodic on the world.

#include "crater_forms.hpp"
#include "low_footprint.hpp"
#include "low_reach.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/lows.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain {
class PreClimate;
}

namespace terrain::lows {

inline constexpr int kCraterClasses = 3;
// The lower bound of each class of diameter (meters), then the upper bound of the last.
inline constexpr std::array<double, kCraterClasses + 1> kCraterDiameters = {400.0, 2'000.0,
                                                                            10'000.0, 60'000.0};

struct Craters {
    std::vector<Crater> sites;
    ReachLattice reach;
};

// The craters of seed on the geography, their wear read on the climate (a mean humidity
// without), clear of the lows taken and of the excluded zone.
[[nodiscard]] Craters makeCraters(std::uint64_t seed, const Geography& geography,
                                  const PreClimate* climate, double density,
                                  const std::vector<LowSite>& taken, const Exclusion& excluded);

// The crater reaching the canonical (x, z) there (nothing out of their reach: their reaches never
// overlap).
[[nodiscard]] CraterShape craterAt(const Craters& craters, const WorldSize& world, double x,
                                   double z);

// The wear of a crater, 0 (fresh) to 1 (worn out), from its age (a draw in [0, 1), most of the
// craters old), the humidity of its climate (P / (P + PET)) and the character of its rock (soft
// and worn at 0): 1 - exp(-2.2 * age * (0.15 + 2.5 * humidity + 0.4 * (1 - character))).
[[nodiscard]] double craterWear(double ageDraw, float humidity, float character);

// The class of a diameter, 0 to kCraterClasses - 1.
[[nodiscard]] int craterClass(double diameter);

} // namespace terrain::lows
