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
    // The most its outline stretches its radius: its lobes and the noise of its foot (kOutline),
    // and the arms of the rift zones of a shield.
    double outline = 0.0;
};
[[nodiscard]] FormRule formRule(VolcanoForm form);
// The most the lobes and the noise of the foot of an edifice stretch its radius, the most the
// arms of the rift zones of a shield do, and those of a field (volcano_fields.cpp: its outline,
// then the lobes of its sheet).
inline constexpr double kOutline = 0.15;
inline constexpr double kArms = 0.6;
inline constexpr double kFieldOutline = 0.2;
inline constexpr double kFieldLobes = 0.42;

// The form of a volcano by its origin, whether it stands at sea, whether it is large and whether
// it stands in rugged land (the intensity of the relief of the geography at least kRugged: no
// plateau of ash nor sheet of lava, no maar, among the mountains); seamounts on the shelves
// (floor above kShelf) rise to islands.
inline constexpr float kRugged = 0.3f;
inline constexpr float kShelf = -700.0f;
[[nodiscard]] VolcanoForm drawForm(VolcanoOrigin origin, bool atSea, bool large, bool rugged,
                                   float floor, std::uint64_t seed);

// The sizes of site (height, radius, crater, craterDepth, elongation, axis, reach) from its
// form, its largeness, its floor and its seed.
void drawSizes(VolcanoSite& site);

} // namespace terrain::volcano_sites
