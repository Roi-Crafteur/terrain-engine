#pragma once

#include "terrain/det_math.hpp" // also refuses fast math in every unit including it
#include "terrain/generation/coast.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/generation/voronoi.hpp"
#include "terrain/generation/water/slot_wall.hpp"
#include "terrain/generation/water_kind.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace terrain {

namespace coast_lattice {
class Lattices;
}

// Seed shared by the export tool and the viewer, so both show the same world.
inline constexpr std::uint64_t kDefaultWorldSeed = 20261019;

// Altitudes are in meters; the sea surface is at y = kSeaLevel.
inline constexpr float kSeaLevel = 0.0f;

// Altitude range of the continental curve: without mountains, the ground stays within
// [kMinBaseHeight - detail, kMaxBaseHeight + detail].
inline constexpr float kMinBaseHeight = -2500.0f;
inline constexpr float kMaxBaseHeight = 600.0f;

// Wavelengths are in meters (one noise lattice cell of the first octave).
struct HeightfieldParams {
    // Continentalness: fbm with 4 octaves at a very low frequency. Land where it is above
    // coastContinentalness, ocean below.
    double continentWavelength = 60000.0;
    float coastContinentalness = 0.0f;

    // Macro continentalness: fbm with 2 octaves at a planetary wavelength, added to the local
    // continentalness as macroContinent * tanh(macro / macroContinentWidth). Away from the
    // zero lines of the fbm the term saturates at +-macroContinent: very large oceans, very
    // large continents, occasional supercontinents and inland seas, separated by macro coasts
    // whose width grows with macroContinentWidth (> 0). The local layer above keeps the
    // coasts, islands and regional variations. 0.5 gives planetary oceans and continents; 0
    // (the default) disables the layer: it is not even evaluated, so the world is
    // bit-identical to one without it.
    float macroContinent = 0.0f;
    double macroContinentWavelength = 10'000'000.0; // 10,000 km
    float macroContinentWidth = 0.1f;

    // Tectonic plates (Voronoi layer): plates.amplitude * PlateSample::continentalness is
    // added to the continentalness too, on top of the local and macro layers, and the
    // collision belts lift the mountain mask so that the ridged noise draws mountain ranges
    // along them, plates.collisionMountainBoost higher than elsewhere. Plates 0.5 with
    // macro 0.25 gives planetary continents and oceans shaped by the plates and modulated by
    // the macro layer. plates.amplitude = 0 (the default) disables the layer: it is not even
    // built, so the world is bit-identical to one without it.
    VoronoiParams plates;

    // Mountain mask: smoothstep(maskLow, maskHigh, fbm) at a medium frequency, faded in
    // from the coast so that no mountain rises from the ocean.
    double maskWavelength = 40000.0;
    float maskLow = -0.10f;
    float maskHigh = 0.20f;

    // Domain warping of the ridged noise coordinates, in meters: bends the crests.
    double warpWavelength = 15000.0;
    double warpAmplitude = 4000.0;

    // Ridged multifractal, always multiplied by the mountain mask. Its first octave draws
    // long crest lines (the chains), the next ones add spurs along them.
    double ridgeWavelength = 24000.0;
    int ridgeOctaves = 7;
    double ridgeSharpness = 4.0;
    float mountainHeight = 4000.0f;

    // Light high-frequency detail, faded out near sea level so that the coastline follows
    // the continentalness exactly.
    double detailWavelength = 200.0;
    float detailAmplitude = 3.0f;

    // The planetary geography of phase 11R (GeographyParams), enabled by default since
    // generator 3: the continents, coasts, ocean floors and regional elevation come from it, and
    // the local, macro and plate terms of the continentalness above are not used (the plates of
    // the geography are its own). Disabled: the relief of generator 2.
    GeographyParams geography;

    // The climate reads the latitude of a sphere (climate_latitude.hpp; flag sphere_latitude): the
    // relief reads it for the fjords of the coasts only. Off, the latitude of main.
    bool sphereLatitude = false;
};

// Layers of the height at one point: height = base + mountain + detail. In the collision
// belts of the plates, mountains are boosted by plates.collisionMountainBoost * plateCollision.
struct HeightSample {
    float continentalness = 0.0f;      // [-1, 1], local plus the macro and plate terms, clamped
    float macroContinentalness = 0.0f; // [-1, 1], raw macro fbm, 0 when disabled
    float plateContinentalness = 0.0f; // PlateSample::continentalness, 0 when disabled
    float plateCollision = 0.0f;       // [0, 1], PlateSample::collision, 0 when disabled
    float mountainMask = 0.0f;         // [0, 1]
    float relief = 0.0f; // [0, 1], the intensity of the relief of the geography on land (else 0)
    float rift = 0.0f;   // [0, 1], GeographySample::rift: the troughs of the rifts (1 on their axis)
    float base = 0.0f;                 // continental curve, [kMinBaseHeight, kMaxBaseHeight]
    // The regional ground under the local relief of the geography (its hills, buttes and forms of
    // the plains: base less them); base without the geography. The seas of dunes of the waves
    // bury the local relief down to it (ergs.hpp).
    float regional = 0.0f;
    float mountain = 0.0f;             // [0, mountainMask * mountainHeight * (1 + boost)]
    float detail = 0.0f; // [-detailAmplitude, detailAmplitude] (with the geography, its own)
    float height = 0.0f;
    // The water over the point: the sea where the geography puts it (its coast signal below 0;
    // without the geography, the ground below kSeaLevel), at the level waterLevel (the height on
    // dry land).
    WaterKind water = WaterKind::None;
    float waterLevel = 0.0f;
    // Meters of sand the seas of dunes lay over the relief (ergs.hpp, part of height): 0 outside
    // them and without hydrology.
    float dunes = 0.0f;
    // The share of the seas of dunes at the column ([0, 1]): their mask (ErgPoint::mask), even
    // between their dunes, lowered to the floor of an erg (kErgFloorShare of it, biome_blend.hpp)
    // on the rock of the geography out of the sand, smoothly up its foot (Ergs::dunesAt): 0
    // outside them and without hydrology.
    float erg = 0.0f;
    // The kind of the floor of the seas of dunes between their dunes (Ergs::dunesAt): 0 sand, 1
    // gravel, 2 crust, continuous between them; 0 outside them, where their dunes are 10 m deep or
    // more (never read there) and without hydrology.
    float ergFloor = 0.0f;
    // Bare rock the coasts lay on the surface, 0 or 1 (the rocks of the reefs and the sea a few
    // meters around them, coast.hpp): the material only, never the height.
    float rock = 0.0f;
};

// A depression below the sea level of the geography (phase 11B): its center (canonical), its mean
// radius and the altitude of its floor, meters, and the direction of its escarpment from the
// center along (x, z) (cos, sin of escarpment turns).
struct Depression {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    float floor = 0.0f;
    double escarpment = 0.0;
};

// Multi-scale terrain height y = heightAt(x, z), in meters.
// Deterministic: the same seed, parameters and coordinates always give the same height.
// Periodic on the world (WorldSize): every layer tiles it, and (x, z) is brought back into its
// canonical range first, so every image of a point gets the same bits.
// With the geography, the origin lies on a continental coast, near a mountain belt when the
// equator has one (Geography::anchorX). Without it, like every fbm layer, the macro
// continentalness is exactly 0 at the origin, so without the plates the origin always lies on a
// macro coast: the local layer alone decides whether it is land or sea.
class Hydrology;   // the water of the world (water/hydrology.hpp)
struct RiverCarve; // what its valleys make of a point (water/hydrology.hpp)
namespace wall_faces {
struct Site; // what the walls report at a point (internal, wall_faces.hpp)
} // namespace wall_faces
namespace mountain_walls {
struct Masks; // the masks of the walls at a point (internal, mountain_walls.hpp)
struct Memo;
} // namespace mountain_walls

class Heightfield {
public:
    // Throws std::invalid_argument for an invalid world (validate).
    explicit Heightfield(std::uint64_t seed, const HeightfieldParams& params = {},
                         const WorldSize& world = {});

    [[nodiscard]] float heightAt(double x, double z) const;
    [[nodiscard]] HeightSample sampleAt(double x, double z) const;

    // sampleAt and heightAt of the points (xs[i], zs[i]), into out[i]: the same bits, faster
    // when consecutive points are close (the rows of a grid or of a tile), as a point reuses the
    // noise gradients of the lattice cells it shares with the previous one. What is reused only
    // lives during the call: nothing is shared between calls or threads, and no result depends
    // on the order of the points. Throws std::invalid_argument unless the spans have one size.
    void samplesAt(std::span<const double> xs, std::span<const double> zs,
                   std::span<HeightSample> out) const;
    void heightsAt(std::span<const double> xs, std::span<const double> zs,
                   std::span<float> out) const;
    // samplesAt with the rock of each point in three dimensions (rock_faces.hpp, phase 12A): the
    // same samples, bit for bit, and the faces the walls lay at each point (and those of the
    // coasts, coast_faces.hpp, agent 33), but those whose run is shorter than leastRun meters (a
    // detail that would not draw them: kRunCells of its cells; the others the same whatever
    // leastRun). rockAt: one point, every face. Throws std::invalid_argument unless the spans
    // have one size.
    void samplesAt(std::span<const double> xs, std::span<const double> zs,
                   std::span<HeightSample> out, std::span<RockColumn> rock,
                   float leastRun = 0.0f) const;
    [[nodiscard]] RockColumn rockAt(double x, double z) const;
    // sampleAt as a level of the horizon of pitch meters shows it (agent 33): where a face of the
    // coasts stands, its height is the top of the rock seen from above as the tiles of LOD 3 draw
    // it (the ledge of the top behind the face, that of its foot in front of it), with a ramp
    // across the face a few pitches of the level wide (coast_faces::seenOf), so that the cliffs
    // neither jump nor move back where the horizon takes over from the tiles and their line is not
    // cut into the staircase of its lattice; bare rock over that ramp. sampleAt bit for bit
    // elsewhere (out of the band of the coasts, away from their faces).
    [[nodiscard]] HeightSample seenAt(double x, double z, double pitch) const;
    // heightsAt as a level of pitch meters shows them (seenAt), for its fine grids (its detail
    // normals and crests): the same bits as heightsAt away from the faces of the coasts.
    void seenHeightsAt(std::span<const double> xs, std::span<const double> zs,
                       std::span<float> out, double pitch) const;
    // How hard the rock is at height y under (x, z), in [0, 1] (0: soft): the layers the walls of
    // the gorges of the rivers cut there (mountain_walls: the hard and soft great bands of the
    // strata and of their rock steps, the soft ones made of beds a few meters thick, hard and
    // soft), the crystalline rock hard. The forms of the falls follow it (phase 12A), never the
    // biomes.
    [[nodiscard]] float rockHardness(double x, double z, double y) const;

    [[nodiscard]] const HeightfieldParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }
    // The seed the relief was made of (the planet level draws the waves of its bands of winds
    // from it).
    [[nodiscard]] std::uint64_t seed() const { return seed_; }

    // The tectonic plates of the continentalness, or nullptr when the layer is disabled
    // (plates.amplitude <= 0, or the geography is enabled).
    [[nodiscard]] const TectonicPlates* plates() const { return plates_.get(); }
    // The planetary geography, or nullptr when it is disabled.
    [[nodiscard]] const Geography* geography() const { return geography_.get(); }
    // The depressions below the sea level, deep in the plains (none without the geography or
    // with GeographyParams::depressions off).
    [[nodiscard]] std::vector<Depression> depressions() const;

    // The Heightfield of the world (phase 11B): this relief with the water of hydrology, built
    // over it: the valleys of its rivers carved, the water of its rivers and lakes. The same bits
    // as this relief wherever no valley lies, but in the band of the coasts near the valleys,
    // where its coasts fade out to the relief without them (leaveValleys).
    [[nodiscard]] Heightfield withHydrology(std::shared_ptr<const Hydrology> hydrology) const;
    // The water of the world, or nullptr for the relief alone.
    [[nodiscard]] const Hydrology* hydrology() const { return hydrology_.get(); }

    // This Heightfield as a grid of samples standing for squares of footprint meters sees it (a
    // level of the far horizon): the seas of dunes filtered to their mean where the grid is
    // coarser than them (Ergs::dunesAt), everything else the same bits. 0: points (the tiles).
    [[nodiscard]] Heightfield forFootprint(double footprint) const;
    [[nodiscard]] double footprint() const { return footprint_; }
    // The coasts (agent 33, coast.hpp): what they make of the point (x, z), any image of it.
    [[nodiscard]] CoastSample coastAt(double x, double z) const;
    // This relief without the coasts (the coastal plains of phase 11R, bit for bit): the relief the
    // planet level and the rivers are built over. A cheap copy sharing the layers and the map.
    [[nodiscard]] Heightfield withoutCoasts() const;
    [[nodiscard]] bool hasCoasts() const { return coasts_ != nullptr; }
    // The inlets cut in the coasts (coast.hpp, CoastSite) whose site lies in the box (canonical
    // meters, the box not wrapping), sorted by x, then z, then their other fields; none without the
    // coasts. Built on demand and kept, from any thread; the same whatever the threads, the order
    // and the boxes of the queries. With a Hydrology, stopped before its rivers and lakes (not
    // without one).
    [[nodiscard]] std::vector<CoastSite> inletsIn(double minX, double minZ, double maxX,
                                                  double maxZ) const;
    // Those of them that are harbors (CoastSite::harbor), for the settlements to come.
    [[nodiscard]] std::vector<CoastSite> harborsIn(double minX, double minZ, double maxX,
                                                   double maxZ) const;
    // Those of them that are anchorages (CoastSite::anchorage; never a harbor), the same way.
    [[nodiscard]] std::vector<CoastSite> anchoragesIn(double minX, double minZ, double maxX,
                                                      double maxZ) const;
    // The stacks off the coasts whose middle lies in the box (the same rules as inletsIn).
    [[nodiscard]] std::vector<CoastStack> stacksIn(double minX, double minZ, double maxX,
                                                   double maxZ) const;

private:
    struct Layers; // the noises of the layers, built once (heightfield.cpp)
    struct Memos;  // the memos of the noises during a batch call (heightfield.cpp)
    struct Water;  // the depressions and the water (heightfield_water.cpp)
    struct Coasts; // the noises of the coasts (heightfield_coast.cpp)
    // What the coasts make of a canonical point of the geography g in reliefWith: its coast
    // signal, the factor of the land (mountains, hills, forms of the plains) and the ground before
    // the local relief; those of the geography out of the band or without the coasts.
    struct CoastPoint {
        bool active = false; // in the band, with the coasts
        double signal = 0.0;
        bool landward = false; // active, on land
        float land = 0.0f;
        float mountains = 0.0f; // the factor of the land of the mountains, never above land
        float ground = 0.0f;
        float fade = 1.0f;      // coast_shape::Ground::fade
        float irregular = 0.0f; // coast_shape::Ground::irregular
        float rock = 0.0f;      // coast_shape::Ground::rock
    };
    [[nodiscard]] CoastPoint coastPointAt(double x, double z, const GeographySample& g,
                                          Memos* memos) const;
    // The weights of the kinds of coast at a canonical point of the band (coast_kinds).
    [[nodiscard]] std::array<float, kCoastKindCount>
    coastKindsAt(double x, double z, const GeographySample& g, Memos* memos) const;
    // Near the rivers, in the band of the coasts: the relief s of the canonical (x, z) faded from
    // its coasts to the relief without them as the valleys, ponds and lakes come near
    // (Hydrology::clearance), before their water: their corridors keep the relief they were found
    // over, bit for bit (heightfield_coast.cpp).
    void leaveValleys(double x, double z, HeightSample& s, Memos* memos) const;
    // A face of the coasts reported at the canonical (x, z), its heights relative to the ground
    // there, anchored on the relief relief there (anchorFace), its weight faded out near the
    // rivers as the coasts are (Hydrology::clearance: no rock over their water).
    void anchorCoastFace(double x, double z, float relief, RockFace& face) const;
    [[nodiscard]] float coastFaceClear(double x, double z, float height) const;
    // How fast the distance to the shore distance of the canonical (x, z) grows per meter there:
    // the base of the geography over its gradient (gx, gz) and the water of the inlets around
    // (coast_inlets::distanceOf), read a meter away along x and along z.
    [[nodiscard]] double distancePerMeter(double x, double z, double base, double gx, double gz,
                                          double distance) const;
    [[nodiscard]] static std::shared_ptr<const Coasts> makeCoasts(std::uint64_t seed,
                                                                  const WorldSize& world);
    // The inlets of these coasts, stopped before the rivers of hydrology_ (heightfield_coast.cpp).
    [[nodiscard]] std::shared_ptr<const coast_lattice::Lattices> makeInlets() const;

    [[nodiscard]] static std::shared_ptr<const Water>
    makeWater(std::uint64_t seed, const HeightfieldParams& params, const Geography* geography);
    // The depressions below the sea and the sea of a sample at a canonical point.
    void finishWater(double x, double z, HeightSample& s) const;
    // The seas of dunes of the hydrology (ergs.hpp) over the relief at a canonical point, before
    // its valleys (heightfield_hydrology): nothing, not a bit, outside them.
    void addDunes(double x, double z, HeightSample& s) const;
    // The valleys, rivers and lakes of the hydrology at a canonical point, last
    // (heightfield_hydrology), through the memos and the region of a batch call (null otherwise).
    // With wall, the wall of the slot of a fall the ground there follows (width 0 otherwise);
    // with gorge, the faces the walls of a gorge lay there (heightfield_rock.cpp: placed as
    // those of the mountains).
    void applyHydrology(double x, double z, HeightSample& s, Memos* memos,
                        SlotWall* wall = nullptr, wall_faces::Site* gorge = nullptr) const;
    // The rock of the walls of a valley of the hydrology over its surface h
    // (heightfield_hydrology).
    [[nodiscard]] double valleyRockAt(double x, double z, double h, const RiverCarve& c,
                                      double show, Memos* memos,
                                      wall_faces::Site* gorge = nullptr) const;
    // Adds to site the faces of the walls of a gorge at (x, z) over its surface h, of masks the
    // masks of their strata (kGorgeFaces, kSteepFaces: heightfield_hydrology.cpp).
    void gorgeFaces(double x, double z, double h, const RiverCarve& c,
                    const mountain_walls::Masks& masks, mountain_walls::Memo* memo,
                    wall_faces::Site& site) const;
    // applyHydrology over the samples of a batch, at the canonical points of (xs[i], zs[i]): the
    // reaches and the lakes of their box found once (Hydrology::Region); with rock, the walls of
    // the slots of the falls at the points (as many) and the faces of the walls of the gorges
    // (the points that have some).
    struct WaterRock {
        std::span<SlotWall> walls;
        std::vector<std::pair<std::size_t, wall_faces::Site>>* gorges = nullptr;
    };
    void waterOf(std::span<const double> xs, std::span<const double> zs,
                 std::span<HeightSample> samples, Memos& memos,
                 const WaterRock* rock = nullptr) const;
    // The carved faces of the rock of the slots of the falls (slot_faces.hpp) at the points of a
    // batch from their walls, those whose run is leastRun meters at least (heightfield_rock.cpp).
    void placeSlotFaces(std::span<const double> xs, std::span<const double> zs,
                        std::span<const SlotWall> walls, float leastRun,
                        std::span<RockColumn> rock) const;

    // sampleAt without the hydrology, through the memos during a batch call (null otherwise).
    // With a site, what the walls of the mountains report at the point (their faces, phase 12A).
    [[nodiscard]] HeightSample reliefWith(double x, double z, Memos* memos,
                                          wall_faces::Site* site = nullptr) const;
    // Places the faces the walls found at (x, z) along the slope of their ground (what the walls
    // report 1 m along x and along z: alongX and alongZ, or probes when null) and anchors them on
    // relief, the relief at the column, into rock, those whose run is leastRun meters at least
    // (heightfield_rock.cpp).
    void placeFaces(double x, double z, Memos& memos, const wall_faces::Site& site,
                    const wall_faces::Site* alongX, const wall_faces::Site* alongZ,
                    float relief, float leastRun, RockColumn& rock) const;
    void continentalnessAt(double x, double z, HeightSample& s, Memos* memos) const;
    [[nodiscard]] float mountainAt(double x, double z, float mask, Memos* memos) const;
    // The warped point of the ridged noise (the warp of the geography when it is enabled).
    [[nodiscard]] std::array<double, 2> warpedAt(double x, double z, Memos* memos) const;
    // reliefWith on the planetary geography (heightfield_geography.cpp), at a canonical point:
    // its regional elevation, the local relief around it and the ridged mountains.
    // With a site, what its walls report.
    [[nodiscard]] HeightSample geographySample(double x, double z, Memos* memos,
                                               wall_faces::Site* site = nullptr) const;
    // The intensity of the relief at the point: that of the map with the spurs of the chains.
    [[nodiscard]] float reliefAt(double x, double z, float mapRelief, Memos* memos) const;
    // The mountains at a point: their height above the base, that of their great forms alone
    // (without the rock and the ravines) and its gradient (meters per meter, up), how much the
    // point lies on the floor of a valley, within a valley and on a crest of the massif, in
    // [0, 1].
    struct MountainForm {
        float height = 0.0f;
        float shape = 0.0f;
        double gradX = 0.0;
        double gradZ = 0.0;
        float floor = 0.0f;
        float valley = 0.0f;
        float crest = 0.0f;
    };
    // The ridged mountains of this intensity and character under the mask, cut by the valleys in
    // U, crowned by the dominant summits (mountain_forms), carved by ravines, over ground meters
    // of regional elevation.
    [[nodiscard]] MountainForm geographyMountainAt(double x, double z, float ground, float mask,
                                                   float intensity, float character,
                                                   Memos* memos) const;
    // A mountain of height mountain (the great forms of form, warped there, with the rock) carved
    // by its ravines; crease: that of the massif, which counts onMassif in [0, 1] (0 in the
    // valleys).
    [[nodiscard]] float mountainRavinesAt(const std::array<double, 2>& warped,
                                          const MountainForm& form, float crease, double onMassif,
                                          float mountain, Memos* memos) const;
    // The depth of the ravines down the faces of the walls of the rifts at a canonical point,
    // meters, at most room.
    [[nodiscard]] float riftRavinesAt(double x, double z, float room, Memos* memos) const;
    // The local relief of a plain where the mountains are not (plain_forms), weight (0, 1] of
    // the way from local, at a canonical point.
    [[nodiscard]] float plainFormsAt(double x, double z, float region, float local, float weight,
                                     Memos* memos) const;
    // The hills, vallons, buttes and terraces of the geography (relief_local).
    [[nodiscard]] float localReliefAt(double x, double z, float amplitude, float buttes,
                                      float region, float plain, float rough,
                                      const CoastPoint& coast, Memos* memos) const;
    [[nodiscard]] float detailAt(double x, double z, float base, Memos* memos) const;
    // The top of the rock with the walls of the mountains (mountain_walls) over ground meters
    // (the base and the great forms of the mountains).
    [[nodiscard]] float wallsAt(double x, double z, float ground, const MountainForm& form,
                                float intensity, float character, Memos* memos,
                                wall_faces::Site* site = nullptr) const;

    HeightfieldParams params_;
    WorldSize world_;
    std::uint64_t seed_ = 0;
    std::shared_ptr<const Layers> layers_;         // shared by copies, immutable
    std::shared_ptr<const TectonicPlates> plates_; // shared by copies, immutable
    std::shared_ptr<const Geography> geography_;   // shared by copies and Heightfields
    std::shared_ptr<const Water> water_;           // shared by copies, immutable
    std::shared_ptr<const Hydrology> hydrology_;   // the water of the world, or null
    double footprint_ = 0.0;                       // of a sample, for the dunes (forFootprint)
    std::shared_ptr<const Coasts> coasts_;         // the coasts, or null without them
    // The inlets and stacks of the coasts, cell by cell on demand (null without the coasts): of
    // this Heightfield and its copies, but those with another Hydrology (withHydrology).
    std::shared_ptr<const coast_lattice::Lattices> inlets_;
};

} // namespace terrain
