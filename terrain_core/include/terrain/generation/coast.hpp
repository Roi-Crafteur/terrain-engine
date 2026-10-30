#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace terrain {

// The coasts of the streamed world (agent 33): the relief of the band along the coasts where the
// geography faded the mountains out (GeographyParams::coastRamp of coast signal inland) and of the
// near sea (kCoastSeaBand of coast signal out at sea). Out of that band every bit of the world is
// that of the relief without coasts.
//
// The geography is never changed: Geography::sampleAt (its coast signal, regional elevation, belts,
// intensity and character of the relief, rifts), its nodes, its plates and its belts keep their
// bits. What the coasts make of a point is read apart, on the Heightfield (Heightfield::coastAt):
// the coast signal with the inlets cut in it (the sea is where it is below 0) and the weights of
// the kinds of coast. The kinds are placed by the geology and the relief of the hinterland (the
// intensity and character of its relief, its regional elevation, its strata or crystalline rock)
// and by the latitude (the fjords in the cold), never by the biomes.
//
// The planet level and the rivers (Hydrology) are built over the relief without coasts
// (Heightfield::withoutCoasts): the coasts never move a river, a fall or a lake; the rivers carve
// their valleys through the coastal relief down to the sea.

// Meters of coast signal out at sea over which the coasts shape the near sea floor.
inline constexpr double kCoastSeaBand = 20'000.0;

enum class CoastKind : std::uint8_t {
    Low,      // low coasts and beaches: the coastal plain
    Rocky,    // low rocky coasts: small cliffs, rock platforms, coves and points
    Cliff,    // sea cliffs before a plateau or hills: straight in strata, cut in crystalline rock
    Calanque, // cliffs of hard young strata (limestone) cut by narrow steep inlets
    Ria,      // rugged coasts of moderate relief: drowned branching valleys
    Mountain, // mountains plunging into the sea
    Fjord,    // mountains plunging into the sea in the cold: long deep troughs
};
inline constexpr int kCoastKindCount = 7;

[[nodiscard]] const char* coastKindName(CoastKind kind);

// What the coasts make of a point (Heightfield::coastAt).
struct CoastSample {
    // The coast signal of the geography (Geography::sampleAt), meters, positive on land.
    double base = 0.0;
    // The coast signal of the world, the inlets cut in it: the sea is exactly where it is below 0.
    double signal = 0.0;
    // Whether the point lies in the band of the coasts (-kCoastSeaBand < base < coastRamp).
    bool inBand = false;
    // The weights of the kinds of coast, in [0, 1], summing to 1 (Low alone out of the band and
    // without the coasts).
    std::array<float, kCoastKindCount> weights{1.0f};
    CoastKind kind = CoastKind::Low; // the heaviest

    [[nodiscard]] float weight(CoastKind k) const { return weights[static_cast<std::size_t>(k)]; }
};

} // namespace terrain
