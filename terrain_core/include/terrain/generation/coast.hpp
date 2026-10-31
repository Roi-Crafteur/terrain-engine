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

// The inlets cut in the coast signal (Heightfield::inletsIn): each a tube of sea rooted out in the
// sea of the geography, going inland up its coast signal (the sea always joins the ocean), its
// width and floor changing along it; stopped before the corridor of any river or lake of the
// Hydrology of the Heightfield (none without one), and before the inner part of the band of the
// coasts. Placed on fixed lattices along the coasts by the kinds of coast, a pure function of
// their cell: the same whatever the threads, the order or the box of the queries.
enum class InletKind : std::uint8_t {
    Fjord,    // fjords: troughs a few kilometers long between plunging mountains, a sill at sea
    Ria,      // rias: drowned valleys narrowing inland, with side branches, shallow
    Calanque, // calanques: narrow winding inlets between the faces of limestone cliffs
    Cove,     // coves: round bays in soft rock between capes, on the rocky coasts and cliffs
};
inline constexpr int kInletKindCount = 4;

[[nodiscard]] const char* inletKindName(InletKind kind);

// An inlet, for the settlements to come: a harbor where it is sheltered (Heightfield::harborsIn).
struct CoastSite {
    double x = 0.0;       // canonical: the middle of its widest water inland of its mouth, meters
    double z = 0.0;
    float opening = 0.0f; // the width of its mouth on the shore of the geography, meters
    float size = 0.0f;    // the width of its widest water inland of the mouth, meters
    float length = 0.0f;  // from its mouth to its head, meters
    float depth = 0.0f;   // of its floor at (x, z), meters below the sea level
    // In [0, 1]: how far it keeps the open sea away: sqrt(1 - opening / size) times
    // length / (length + opening / 2); 0 where its mouth is no narrower than its inside.
    float shelter = 0.0f;
    InletKind inlet = InletKind::Cove;
    CoastKind coast = CoastKind::Rocky; // the kind of the coast at its mouth
    bool harbor = false;                // sheltered: kHarborShelter and kHarborDepth at least
};

// A harbor: shelter at least kHarborShelter, depth at least kHarborDepth meters, mouth at least
// kHarborOpening meters wide and narrower than the water inside.
inline constexpr float kHarborShelter = 0.5f;
inline constexpr float kHarborDepth = 3.0f;
inline constexpr float kHarborOpening = 20.0f;

} // namespace terrain
