#pragma once

#include "terrain/generation/cave_materials.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace terrain {

// Cave regions, inspired by YUNG's Better Caves: the underground is cut into districts, the
// Voronoi cells of the sites of a jittered world grid (2 to 4 km across), each of a type that
// shapes its caves. Named districts in the code: a CaveRegion (cave_region.hpp) is the area of
// caves under an eroded region. A district only changes parameters of the caves (CaveProfile),
// blended smoothly across its borders: never the limits of the caves (roof, depth, rivers,
// oceans), so the surface stays closed wherever it was.
enum class CaveDistrictType : std::uint8_t {
    Tunnels,   // wide spaghetti tunnels and many noodles
    Halls,     // large cheese caverns
    Crevasses, // ravines everywhere along their lines
    Lakes,     // wetter aquifers with higher levels: large lakes
    Rivers,    // wet tunnels (the underground rivers come with Phase 8C-2)
    Flooded,   // aquifers always full, up to just under the roof
    Volcanic,  // dry basalt caves (the lava comes with Phase 8C-2)
    Glacier,   // ice walls, frozen lakes, ice speleothems
    Lush,      // moss, cave flora, wet caves
    Dripstone, // many long speleothems
};
inline constexpr int kCaveDistrictTypeCount = 10;

// Lowercase English name of a district type, such as "dripstone".
[[nodiscard]] const char* caveDistrictName(CaveDistrictType type);

// Parameters of the caves that a district changes, as a vector of floats so that they blend
// field by field. The neutral profile (Default) leaves every cave exactly as without districts:
// scales multiply final values, offsets are added to non-zero thresholds.
struct CaveProfile {
    enum Field : std::size_t {
        kSpaghettiScale,   // multiplies the radius of the spaghetti tunnels (1)
        kNoodleOffset,     // added to the presence threshold of the noodles (0): lower, more
        kCheeseOffset,     // added to the threshold of the cheese caverns (0): lower, larger
        kRavineBoost,      // added to the strength of the ravines, clamped to 1 (0)
        kWetOffset,        // added to the wet chance of the aquifers (0)
        kLevelRise,        // meters the aquifer levels rise (0)
        kFlooded,          // [0, 1]: toward always wet, level just under the roof (0)
        kIce,              // [0, 1]: ice walls, frozen water and speleothems from 0.5 (0)
        kLush,             // [0, 1]: moss from 0.5, cave flora in proportion (0)
        kVolcanic,         // [0, 1]: basalt from 0.5 (0)
        kSpeleothemChance, // multiplies the chance of the speleothems (1)
        kSpeleothemLength, // multiplies their largest length (1)
        kFieldCount,
    };

    std::array<float, kFieldCount> values{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};

    [[nodiscard]] float operator[](Field field) const { return values[field]; }
    float& operator[](Field field) { return values[field]; }
    [[nodiscard]] bool icy() const { return values[kIce] >= 0.5f; }
    [[nodiscard]] bool lush() const { return values[kLush] >= 0.5f; }
    [[nodiscard]] bool volcanic() const { return values[kVolcanic] >= 0.5f; }

    friend bool operator==(const CaveProfile&, const CaveProfile&) = default;
};

// Profile of each district type (index: CaveDistrictType).
[[nodiscard]] std::array<CaveProfile, kCaveDistrictTypeCount> defaultDistrictProfiles();

// Cave flora of the lush regions (cave_flora.hpp): at most one candidate per cell of a world grid
// of spacing meters, moved by up to jitter / 2 of the cell, kept with probability chance times
// the lush field of the profile; ferns and mushrooms on the floor of the highest dry void under
// it not open to the sky, vines hanging from its ceiling, sized up to maxSize and maxShare of
// the free height, never in the column of an entrance or in ice. Objects: never geometry.
struct CaveFloraParams {
    double spacing = 3.0;
    double jitter = 0.8;
    float chance = 0.6f;
    float minSize = 0.4f;
    float maxSize = 1.6f;
    float minSpan = 2.5f;
    float maxShare = 0.4f;
    float entranceMargin = 1.0f;
};

struct CaveDistrictParams {
    // Sites: one per cell of a world grid of cellSize meters, moved by up to jitter / 2 of the
    // cell along each axis.
    double cellSize = 3000.0;
    double jitter = 0.5;
    // Blend: a district weighs 1 - smoothstep(0, blend, d - dmin), d its distance to the
    // point and dmin the nearest one: exactly its own profile farther than blend from the other
    // sites (by that difference), a smooth mix near its borders. At most 0.189 of the cell, so
    // that the 3 x 3 cells around a point hold every district weighing on it.
    double blend = 280.0;
    // Flooded districts only on ground lower than this at their site.
    float floodedMaxHeight = 150.0f;
    std::array<CaveProfile, kCaveDistrictTypeCount> profiles = defaultDistrictProfiles();
    CaveFloraParams flora;
};

// A district: its cell, site and type.
struct CaveDistrict {
    int cellX = 0;
    int cellZ = 0;
    double siteX = 0.0;
    double siteZ = 0.0;
    CaveDistrictType type = CaveDistrictType::Tunnels;

    friend bool operator==(const CaveDistrict&, const CaveDistrict&) = default;
};

// What a district reads at its site: the ground height and the environment above.
using DistrictSite = std::function<void(double x, double z, float& height,
                                        CaveEnvironment& environment)>;

// Weights of the districts at a point: up to 9 districts (the 3 x 3 cells around it), the
// first being the nearest; the weights sum to 1.
struct DistrictWeights {
    std::array<CaveDistrict, 9> districts{};
    std::array<float, 9> weights{};
    int count = 0;
};

// Deterministic districts: the same seed, parameters and ground always give the same district
// and the same profile at a point, wherever the area being generated is.
class CaveDistricts {
public:
    // Throws std::invalid_argument for a blend wider than 0.189 of the cell or a jitter above
    // 0.5.
    CaveDistricts(std::uint64_t seed, const CaveDistrictParams& params);

    // District of the cell (cellX, cellZ): the type is drawn with weights from the environment
    // and the height at its site (glaciers only in the cold, lush districts humid and warm,
    // dripstone humid, flooded on low ground, volcanic rare).
    [[nodiscard]] CaveDistrict district(int cellX, int cellZ, const DistrictSite& site) const;

    // Weights at (x, z), from the districts of the 3 x 3 cells around it given by districtAt.
    [[nodiscard]] DistrictWeights weights(
        double x, double z, const std::function<const CaveDistrict&(int, int)>& districtAt) const;
    // Profile at (x, z): the profiles of the weights, blended. Exactly the profile of the type
    // where a single district weighs.
    [[nodiscard]] CaveProfile profile(const DistrictWeights& weights) const;

    [[nodiscard]] const CaveDistrictParams& params() const { return params_; }

private:
    CaveDistrictParams params_;
    std::uint64_t seed_;
};

} // namespace terrain
