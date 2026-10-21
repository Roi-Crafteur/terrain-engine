#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/climate.hpp"
#include "terrain/generation/heightfield.hpp"

#include <cstdint>
#include <unordered_map>

namespace terrain {

// Materials of the walls of the caves and of their lakes. They only color the caves: they
// never change the density, the caves or the relief.
enum class CaveMaterial : std::uint8_t {
    Surface,   // the outer surface of the terrain, colored by its own layers
    Stone,
    Deepstone, // deep down
    Limestone, // humid ground: the rock of the karst caves
    Sandstone, // under the deserts, savannas and beaches
    Ice,       // frozen caves, frozen lakes and their shores
    Moss,      // near the surface under humid forests and marshes
    Mud,       // next to a lake
    Gravel,    // dry floors
    Water,     // the surface of a lake
    Basalt,    // volcanic cave regions
    Magma,     // next to a lava lake
};
inline constexpr int kCaveMaterialCount = 12;

// Lowercase English name of a material, such as "deepstone".
[[nodiscard]] const char* caveMaterialName(CaveMaterial material);

// What a cave reads from the surface above it: the biome, the temperature (corrected by the
// altitude) and the humidity of the climate.
struct CaveEnvironment {
    Biome biome = Biome::Grassland;
    float temperature = 12.0f;
    float humidity = 0.5f;

    friend bool operator==(const CaveEnvironment&, const CaveEnvironment&) = default;
};

// Thresholds of the materials. Depths are in meters below the ground of the column.
struct CaveMaterialParams {
    float surfaceDepth = 1.5f;   // shallower: Surface
    float wetHeight = 3.0f;      // at most this far above the level of a lake: Mud (or Ice)
    float iceTemperature = -2.0f;
    float iceDepth = 30.0f;      // frozen caves down to this depth
    float mossDepth = 20.0f;
    float mossHumidity = 0.6f;
    float floorNormal = 0.7f;    // floors: the normal points up at least this much
    float deepDepth = 70.0f;
    float limestoneHumidity = 0.45f;
    float lushDepth = 40.0f; // moss down to this depth in lush cave regions
    float magmaHeight = 3.0f; // walls up to this height above a lava lake glow
};

// A point of a cave wall.
struct CaveWallSite {
    CaveEnvironment environment;
    float depth = 0.0f;
    float normalY = 0.0f;        // of the wall, toward the air: positive on floors
    float aboveWater = 1.0e9f;   // height above the level of the lake of its aquifer, if wet
    bool frozenWater = false;    // that lake is frozen
    float aboveLava = 1.0e9f;    // height above the level of the lava of its aquifer, if any
    // The cave region of the wall (CaveProfile::icy, lush, volcanic; all false without regions).
    bool icyRegion = false;
    bool lushRegion = false;
    bool volcanicRegion = false;
};

// Material of a wall, by priority: the outer surface, magma next to a lava lake, ice (frozen lake
// shores, cold shallow caves, glacier regions), mud next to a lake, basalt in volcanic regions,
// moss in lush regions
// (down to lushDepth), sandstone under dry biomes, moss near the surface under humid forests and
// marshes, gravel on floors, deepstone deep down, limestone in humid climates, stone otherwise.
[[nodiscard]] CaveMaterial caveMaterial(const CaveWallSite& site,
                                        const CaveMaterialParams& params = {});

// Environment of the caves of a heightfield: the biome and climate of the surface above, at
// the nodes of a world lattice of kCaveEnvironmentSpacing meters (each node computed from the
// heightfield at its position alone, as surfaceFromHeightfield: no seam), the temperature and
// humidity interpolated bilinearly in between and the biome of the nearest node.
inline constexpr double kCaveEnvironmentSpacing = 16.0;

class CaveEnvironmentField {
public:
    // The heightfield and the climate must outlive the field. Periodic on their world: the
    // nodes fall exactly on its sides and every node takes the value of its canonical image.
    // Throws std::invalid_argument if the climate and the heightfield have different worlds.
    CaveEnvironmentField(const Heightfield& field, const ClimateField& climate,
                         const BiomeParams& biomes = {});

    // Environment of the node (i, k), at (i, k) * kCaveEnvironmentSpacing.
    [[nodiscard]] CaveEnvironment node(std::int64_t i, std::int64_t k) const;
    [[nodiscard]] CaveEnvironment at(double x, double z) const;

    [[nodiscard]] const PeriodicCells& nodesX() const { return nodesX_; }
    [[nodiscard]] const PeriodicCells& nodesZ() const { return nodesZ_; }

private:
    const Heightfield* field_;
    const ClimateField* climate_;
    BiomeParams biomes_;
    PeriodicCells nodesX_;
    PeriodicCells nodesZ_;
};

// The nodes of a field read so far, kept: the same values as the field, computed once.
class CaveEnvironmentCache {
public:
    explicit CaveEnvironmentCache(const CaveEnvironmentField& field) : field_(&field) {}

    [[nodiscard]] CaveEnvironment at(double x, double z);

private:
    [[nodiscard]] const CaveEnvironment& node(std::int64_t i, std::int64_t k);

    const CaveEnvironmentField* field_;
    std::unordered_map<std::uint64_t, CaveEnvironment> nodes_;
};

} // namespace terrain
