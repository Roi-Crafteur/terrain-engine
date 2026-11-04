#pragma once

// Internal to the volcanoes (volcanoes.hpp): the draws of a volcano, its form by its origin and
// place, then its sizes, from its own seed. Pure functions of their arguments.

#include "terrain/generation/volcanoes.hpp"

#include <cstdint>

namespace terrain::volcano_sites {

// The stream of the shapes of the volcanoes of a world ("VOLC"), apart from every layer of the
// relief and from the draws of the settlement.
inline constexpr std::uint64_t kShapeStream = 0x564F4C43;

// A draw in [0, 1) of the stream seed, index by index.
[[nodiscard]] double unit(std::uint64_t seed, std::uint64_t index);
// A draw in [low, high).
[[nodiscard]] double between(std::uint64_t seed, std::uint64_t index, double low, double high);

// The seed of the volcano centered at the canonical (x, z): its position hashed, so that its
// shapes never depend on the order of the list.
[[nodiscard]] std::uint64_t siteSeed(std::uint64_t stream, double x, double z);

// How a form lays its relief (volcano_relief.cpp), and so how far it reaches (drawSizes).
struct FormRule {
    bool edifice = false; // a cone or a dome over its foot (otherwise a field: cones, sheets, maars)
    bool shield = false;  // the dome of a shield, otherwise the concave cone of a stratovolcano
    double apron = 1.0;   // the edifice fades out from its foot (rho 1) to rho apron
};
[[nodiscard]] FormRule formRule(VolcanoForm form);
// The most the outline of an edifice stretches its radius (its lobes and the noise of its foot),
// and that of a field (volcano_fields.cpp: its outline, then the lobes of its sheet).
inline constexpr double kOutline = 0.15;
inline constexpr double kFieldOutline = 0.2;
inline constexpr double kFieldLobes = 0.42;

// The form of a volcano by its origin, whether it stands at sea and whether it is large.
[[nodiscard]] VolcanoForm drawForm(VolcanoOrigin origin, bool atSea, bool large,
                                   std::uint64_t seed);

// The sizes of site (height, radius, crater, craterDepth, elongation, axis, reach) from its
// form, its largeness, its floor and its seed.
void drawSizes(VolcanoSite& site);

} // namespace terrain::volcano_sites
