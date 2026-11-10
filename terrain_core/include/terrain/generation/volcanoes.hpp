#pragma once

// The volcanoes of the world (agent 45, phase 12): a province of the relief at the places the
// plate tectonics puts them (src/generation/volcano_placement.hpp: the arcs behind the
// subductions of oceanic plates, island arcs included, the continental rifts, the ridges reaching
// the land, the hotspots anywhere), drawn from the seed with the constants of the core: the same
// positions and the same active and large ones the settlement reads. Never placed by the biome,
// never read from data.
//
// Each volcano draws its form from its own seed, by its origin, whether it stands at sea (the
// coast signal of the geography at its center below 0) and whether it is large: stratovolcanoes,
// twin ones and ones scarred by the collapse of a flank, shields, fields of cinder cones, of
// lava, rows of fissures, fields of maars, calderas on an edifice or on a plateau, volcanic
// islands and seamounts. Its sizes come from real volcanoes (VolcanoSite).
//
// Where no volcano reaches (VolcanoSite::reach around its center) the world keeps every bit.
// Deterministic, thread-safe, periodic on the world.

#include "terrain/generation/geography_params.hpp"
#include "terrain/generation/water_kind.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace terrain {

class Geography;

// The counts of the reference planet and the rules of the placement: the constants of the core
// (their defaults), with which the settlement places its volcanoes too (volcano_positions_test
// keeps their positions), whatever the data. A Heightfield may change them for itself.
struct VolcanoParams {
    struct Count {
        int min = 0;
        int max = 0;
        friend bool operator==(const Count&, const Count&) = default;
    };
    // Off: no volcano, not even placed (the world before agent 45, bit for bit). Off by default
    // until R1, the pull request that turns the relief of the agents of wave 9 on together; the
    // flag "volcanoes" of the experimental flags turns it on before.
    bool enabled = false;
    double referenceAreaKm2 = 2'101'000'000.0;
    Count volcanoes{3000, 6000};
    Count active{300, 800};
    Count large{50, 150};
    double arcMinKm = 120.0; // the arcs, behind the boundary on the overriding plate
    double arcMaxKm = 320.0;
    double riftShare = 0.15;    // of the volcanoes, along the rifts and the ridges
    double hotspotShare = 0.05; // of the volcanoes, anywhere
    double spacingKm = 25.0;    // between two volcanoes, at least

    friend bool operator==(const VolcanoParams&, const VolcanoParams&) = default;
};

// Where the plates put a volcano (settlement::VolcanoKind, the same order).
enum class VolcanoOrigin : std::uint8_t { Arc, Rift, Ridge, Hotspot };

enum class VolcanoForm : std::uint8_t {
    Strato,          // a steep concave cone (Fuji, Rainier, Mayon)
    TwinStrato,      // two summits side by side
    CollapsedStrato, // a horseshoe amphitheater open down a flank (Mount St. Helens)
    Shield,          // broad and gentle, a caldera at its summit (Mauna Loa, Skjaldbreidur)
    CinderField,     // tens of cinder cones along fissures (Chaine des Puys)
    LavaField,       // a sheet of lava filling the hollows, cones and flows (Craters of the Moon)
    FissureRow,      // a long row of craters and a field of lava along a rift (Laki)
    MaarField,       // explosion craters dug below the ground, their rings of tuff (Eifel)
    Caldera,         // a collapsed edifice, its walls around a floor (Crater Lake, Ngorongoro)
    PlateauCaldera,  // a caldera dug into a low plateau of ash (Yellowstone)
    IslandStrato,    // a stratovolcano rising from the sea floor
    IslandShield,    // a shield rising from the sea floor (Hawaii)
    IslandCaldera,   // a ring of islands around a drowned caldera (Santorini)
    Seamount,        // a volcano whose summit stays under the sea
};
inline constexpr int kVolcanoFormCount = 14;

// Lowercase English names, such as "strato" or "island caldera".
[[nodiscard]] const char* volcanoFormName(VolcanoForm form);
[[nodiscard]] const char* volcanoOriginName(VolcanoOrigin origin);

// The ground the volcanoes leave (HeightSample::volcanicGround), decided by the core whatever the
// biome: the data the materials read, kept as such for the colors to come (agent 49).
enum class VolcanicGround : std::uint8_t {
    None,
    Basalt,     // old lava and the walls of the calderas, weathered: black to dark gray
    Ash,        // ash and tuff, the upper slopes of the cones, the floors of the calderas: gray-black
    Scoria,     // the cinders of the cones and of the active craters: red-brown to black
    CooledLava, // the flows and sheets of the active volcanoes: black
    Geothermal, // sinter and altered ground around the springs: white to orange
};
inline constexpr int kVolcanicGroundCount = 6;
// Lowercase English names, such as "scoria" or "cooled lava".
[[nodiscard]] const char* volcanicGroundName(VolcanicGround ground);

// The geothermal features of the active volcanoes: where they are, their pools, pots and
// terraces; nothing moves (no jet, no steam).
enum class GeothermalKind : std::uint8_t {
    HotSpring, // a pool of hot water on its terrace of sinter
    Geyser,    // a small pool at the top of its mound of sinter
    MudPot,    // a pit of mud, without standing water
    Fumarole,  // a vent of steam: its ground alone
};
// Lowercase English names, such as "hot spring" or "mud pot".
[[nodiscard]] const char* geothermalKindName(GeothermalKind kind);

// A geothermal feature (Heightfield::geothermalIn).
struct GeothermalSite {
    double x = 0.0; // canonical
    double z = 0.0;
    GeothermalKind kind = GeothermalKind::HotSpring;
    float radius = 0.0f; // of its pool, pot or vent
    // The water of its pool, meters (hot springs and geysers); the ground at its vent otherwise.
    float level = 0.0f;
    std::uint32_t volcano = 0; // in Volcanoes::sites()
};

// A crater, caldera or maar (Heightfield::volcanoBasinsIn) and its lake.
struct VolcanoBasin {
    double x = 0.0; // its center, canonical
    double z = 0.0;
    float radius = 0.0f; // of its rim, on average
    float floor = 0.0f;  // its lowest ground
    // The most a lake in it may rise: the lowest point of its rim, or the bottom of its breach,
    // less a margin. A lake never stands above it.
    float spill = 0.0f;
    // Its lake under the humidity of the climate of the planet level at its center: a single
    // level, between its floor and spill; NaN (and water None) when dry or without the planet
    // level.
    float level = 0.0f;
    WaterKind water = WaterKind::None; // Lake or SaltLake
    bool maar = false;
    std::uint32_t volcano = 0; // in Volcanoes::sites()
};

// A volcano and the sizes its form drew (meters, radians).
struct VolcanoSite {
    double x = 0.0; // its center, in the canonical range
    double z = 0.0;
    VolcanoOrigin origin = VolcanoOrigin::Arc;
    VolcanoForm form = VolcanoForm::Strato;
    bool active = false;
    bool large = false;
    bool atSea = false; // the coast signal of the geography below 0 at its center
    std::uint64_t seed = 0; // the draws of its shapes
    // On land, how high the edifice rises over the ground around it; at sea (islands and
    // seamounts), the altitude of its summit (above 0 for an island).
    float height = 0.0f;
    // The regional ground of the geography at its center (its sea floor at sea): the base of an
    // edifice standing at sea.
    float floor = 0.0f;
    float radius = 0.0f;      // the mean radius of its foot (of its field for the fields)
    float crater = 0.0f;      // the radius of its crater or caldera, 0 for none
    float craterDepth = 0.0f; // how deep its crater or caldera lies under its rim
    float elongation = 1.0f;  // >= 1: the long radius over the short one
    // The sharpness k of the concave profile of a cone, (e^(-k rho) - e^-k) / (1 - e^-k): its
    // slope at the summit k / (1 - e^-k) times its mean slope, at its foot e^-k of that at the
    // summit. 0 for the shields and the fields.
    float sharpness = 0.0f;
    float axis = 0.0f;        // the direction of the long axis, radians from +x toward +z
    float reach = 0.0f;       // nothing of it lies farther from its center
    // The velocity of the plate it stands on (the plates of the geography, made again for the
    // placement: TectonicPlates, their arbitrary units, about 1): where its older volcanoes drift
    // to. The chains of the oceanic hotspots (agent 51) grow along it.
    float driftX = 0.0f;
    float driftZ = 0.0f;
};

// The volcanoes of a world, placed once (from its geography and its seed) and looked up by a grid
// of buckets of kBucket meters, each holding the volcanoes whose reach touches it.
class Volcanoes {
public:
    static constexpr double kBucket = 65536.0; // divides every side of a world exactly

    // The volcanoes of the world of geo, whose Heightfield has the seed worldSeed, built on up to
    // threads threads (0: the hardware concurrency; the same bits whatever their number). None
    // when params are off.
    Volcanoes(std::uint64_t worldSeed, const Geography& geo, const VolcanoParams& params,
              int threads = 0);
    // Those of these arguments, built once and shared while a Heightfield holds them (the last
    // two kept a little longer), as Geography::shared. Thread-safe.
    [[nodiscard]] static std::shared_ptr<const Volcanoes>
    shared(std::uint64_t worldSeed, const std::shared_ptr<const Geography>& geo,
           const VolcanoParams& params);

    [[nodiscard]] const std::vector<VolcanoSite>& sites() const { return sites_; }
    [[nodiscard]] const VolcanoParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }
    // False where no volcano reaches the canonical (x, z) for sure: one bucket read.
    [[nodiscard]] bool mayHold(double x, double z) const;
    // The indices of the volcanoes whose reach touches the bucket of the canonical (x, z), in
    // the order of sites().
    [[nodiscard]] std::span<const std::uint32_t> near(double x, double z) const;
    // The oceanic hotspots (origin Hotspot, at sea), in the order of sites(): the active heads of
    // the chains of agent 51 (seas). The boundary between both: this module alone places every
    // volcano (volcano_placement.hpp, at sea too) and shapes the head of each chain, an
    // island shield or a seamount, within its reach (VolcanoSite::reach: its edifice, crater,
    // flows and satellite cones, its land above the sea); agent 51 lays the older chain behind it
    // along the drift of its plate (VolcanoSite::driftX, driftZ: subsiding islands, guyots,
    // atolls) in the relief of the sea floor, before the volcanoes, so that the head stands on it
    // (the smooth maximum of the edifice and the ground). Agent 51 places no active volcano and
    // never moves one; within the reach of a head, the head rules.
    [[nodiscard]] std::vector<VolcanoSite> oceanHotspots() const;
    // The volcanoes whose center lies in the box (canonical meters, the box not wrapping),
    // sorted by x, then z.
    [[nodiscard]] std::vector<VolcanoSite> in(double minX, double minZ, double maxX,
                                              double maxZ) const;
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    [[nodiscard]] std::size_t bucketOf(double x, double z) const;
    void index();

    VolcanoParams params_;
    WorldSize world_;
    std::vector<VolcanoSite> sites_;
    std::int64_t bucketsX_ = 1;
    std::int64_t bucketsZ_ = 1;
    std::vector<std::uint32_t> starts_; // bucketsX * bucketsZ + 1 offsets into entries_
    std::vector<std::uint32_t> entries_;
};

// The offset from (fromX, fromZ) to (toX, toZ) on the torus of the world, the shortest one:
// local coordinates around a center, never a phase written in world coordinates.
struct VolcanoOffset {
    double dx = 0.0;
    double dz = 0.0;
};
[[nodiscard]] VolcanoOffset volcanoOffset(const WorldSize& world, double fromX, double fromZ,
                                          double toX, double toZ);
// Where the summit of a volcano lies from its center (its crater or caldera around it): its shape
// draws it off the center, never by more than 0.12 of its radius.
[[nodiscard]] VolcanoOffset volcanoSummit(const VolcanoSite& site);

} // namespace terrain
