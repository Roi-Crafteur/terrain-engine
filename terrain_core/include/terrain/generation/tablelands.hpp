#pragma once

// The provinces of the plateaus and of the karst (agent 46, phase 12): the plateaus, mesas,
// buttes and canyons of the layered rock, the karst of the limestone (towers, cockpits, dolines,
// poljes, dry valleys). Placed by the geology and the climate at the scale of the planet, never by
// the biome:
//   - the rock: the share of layered rock of the map (mountain_walls::strataShare, the field the
//     walls of the mountains, the gorges and the coasts read: one geology for the whole world) and
//     a share of limestone in it (a slow noise of the cover);
//   - the ground: plateaus on the uplifted land of moderate relief (not the plains of the coasts,
//     nor the belts of the mountains, nor the troughs of the rifts), karst wherever limestone lies
//     in a humid and warm enough climate, on the plains too;
//   - the climate: the pre-climate of the geography (pre_climate.hpp), never that of the planet
//     level (built from the relief: a loop) nor the biome; the dry lands make sharp mesas and
//     buttes, the hot and humid ones towers.
// A slow noise of the supply of each province (patches, never bands of latitude), a warp and a
// noise of their edges (no line of the coarse grid), and a fade from the coast (the coasts are
// agent 33's band). With the plateaus come the high surfaces of the plateaus behind the collisions
// (the geography keeps their heart below the mountains, tableland_forms.hpp). Computed once per seed on the grid of the pre-climate (the nodes of the map of
// the geography), read at every point. Where mayHold is false (one byte read) every share is
// exactly 0: the world keeps every bit of its relief there. Deterministic, thread-safe, periodic
// on the world.

#include "terrain/generation/geography.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/pre_climate.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {

// The seeds the provinces read: their own draws, the seed of their geography (Geography::shared)
// and that of the noises of the walls (mountain_walls::makeNoises: the share of layered rock).
struct TablelandSeeds {
    std::uint64_t own = 0;
    std::uint64_t geography = 0;
    std::uint64_t walls = 0;

    friend bool operator==(const TablelandSeeds&, const TablelandSeeds&) = default;
};

// The potentials of a cell of the grid, in [0, 1], before the warp and the edges of the points.
struct TablelandCell {
    float tableland = 0.0f;
    float karst = 0.0f;
    float arid = 0.0f;     // the style of the dry lands (1 - humid)
    float tropical = 0.0f; // the hot and humid lands
    // 1 where a high surface of a plateau may lie near the center of the cell (its land high
    // enough, out of the belts and the rifts), else 0: the reach of mayHold.
    float surface = 0.0f;

    friend bool operator==(const TablelandCell&, const TablelandCell&) = default;
};

// What the provinces make of a point (Tablelands::at), each in [0, 1].
struct TablelandPoint {
    float tableland = 0.0f; // the plateaus, mesas, buttes and canyons
    float karst = 0.0f;     // the karst
    float arid = 0.0f;      // how dry: sharp mesas and buttes of the deserts
    float tropical = 0.0f;  // how hot and humid: the towers
    float carbonate = 0.0f; // the share of limestone of the cover
    // The high surface of a plateau behind a collision (tableland_forms: surfaceShare): its heart
    // below the mountains, its inner ranges (the plateaus of the tablelands flag).
    float surface = 0.0f;

    [[nodiscard]] bool any() const {
        return tableland > 0.0f || karst > 0.0f || surface > 0.0f;
    }
};

class Tablelands {
public:
    // The provinces of geography (its parameters say which: GeographyParams::tablelands and
    // ::karst), from its pre-climate. The construction runs on up to threads threads (0: the
    // hardware concurrency), the same bits whatever their number.
    Tablelands(const TablelandSeeds& seeds, std::shared_ptr<const Geography> geography,
               std::shared_ptr<const PreClimate> climate, int threads = 0);
    ~Tablelands();
    Tablelands(const Tablelands&) = delete;
    Tablelands& operator=(const Tablelands&) = delete;

    // The provinces of these arguments (Geography::shared, PreClimate::shared), built once and
    // shared while held. Thread-safe.
    [[nodiscard]] static std::shared_ptr<const Tablelands>
    shared(const TablelandSeeds& seeds, const GeographyParams& params, const WorldSize& world);

    // False where no province can reach the canonical (x, z): the quick test before anything
    // else (one byte of the grid read).
    [[nodiscard]] bool mayHold(double x, double z) const;
    // The provinces at the canonical (x, z), g the geography there (Geography::sampleAt): all 0
    // where mayHold is false.
    [[nodiscard]] TablelandPoint at(double x, double z, const GeographySample& g) const;
    // The same, reading the geography.
    [[nodiscard]] TablelandPoint at(double x, double z) const;

    [[nodiscard]] const TablelandSeeds& seeds() const;
    [[nodiscard]] std::shared_ptr<const Geography> sharedGeography() const;
    [[nodiscard]] std::shared_ptr<const PreClimate> sharedClimate() const;
    [[nodiscard]] const PlanetGrid& grid() const;
    [[nodiscard]] const TablelandCell& cell(std::size_t c) const;
    [[nodiscard]] const Geography& geography() const;
    [[nodiscard]] const PreClimate& climate() const;

    // 64-bit FNV-1a of the raw bits of the potentials and of the lattice of mayHold.
    [[nodiscard]] std::uint64_t checksum() const;
    [[nodiscard]] std::size_t memoryBytes() const;

    struct Impl; // tablelands.cpp

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace terrain
