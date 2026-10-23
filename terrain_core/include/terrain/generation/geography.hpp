#pragma once

#include "terrain/generation/geography_params.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {

// The geography at one point: the continental signal, whose zero line is the coast, and the
// regional fields of the map around it (cubic B-spline of its nodes: smooth, C2).
struct GeographySample {
    // Signed distance to the coast in meters, positive on land: the coast of the map plus the
    // coasts of every scale (GeographyParams). The ground is below the sea exactly where it is
    // negative.
    double coast = 0.0;
    float mapCoast = 0.0f; // the distance to the coast of the map alone
    // Regional elevation of the land, also over the sea: drained, but in the troughs of the rifts
    // (closed basins), at least GeographyParams::landMin.
    float land = 1.0f;
    float sea = 0.0f;  // floor of the ocean, meters (the shelf depth under the land)
    float belt = 0.0f;   // [0, 1], the mountain belts of the collisions of the plates
    float relief = 0.0f;    // [reliefFloor, 1], the intensity of the relief (GeographyParams)
    float character = 0.5f; // [0, 1], from worn and round (0) to young and sharp (1)
    float regions = 0.0f;   // [-1, 1], lands of vallons (above 0) or of buttes (below)
    float rugged = 1.0f; // the share of the amplitude of the small coasts, smoothRugged to 1
    float coarse = 0.0f; // the broadest octaves of the coarse coast noise
    float rift = 0.0f;   // [0, 1], the troughs of the rifts (1 on their axis)
    float riftFloor = 0.0f; // the level of the floor of the rifts, meters, where they reach
};

// The planetary geography of a seed (phase 11R): a map of the whole world computed once,
// deterministic to the bit on every platform, then sampled at any point. Its continents do not
// follow the tectonic plates: a crust noise thresholded to a share of land, chosen among a few
// streams for its count of continents. Its regional elevation drains: it climbs from the coast
// at least GeographyParams::drainSlope along the way water takes to the sea, so the map holds no
// closed depression, but for the troughs of the rifts, cut afterward (the relief first: they
// are not filled for the water). Its tectonic plates raise mountain belts (or foothills only)
// and high plateaus along their collisions (island arcs and trenches at sea), rifts and
// mid-ocean ridges along their divergences. The point (0, 0) lies on a coast of a continent,
// near a mountain belt when one comes close enough: the map is shifted along x to put one there.
class Geography {
public:
    // Fields of a node of the map, in this order.
    enum Field : std::size_t {
        kCoast,
        kLand,
        kSea,
        kBelt,
        kRelief,
        kCharacter,
        kRugged,
        kCoarse,
        kRegions,
        kRift,      // the troughs of the rifts, [0, 1]
        kRiftFloor, // the level of their floors (meters, where their shoulders reach)
        kFields
    };

    // Throws std::invalid_argument for invalid parameters or world. The construction runs on
    // up to threads threads (0: the hardware concurrency), in fixed blocks: the same bits
    // whatever their number.
    Geography(std::uint64_t seed, const GeographyParams& params, const WorldSize& world,
              int threads = 0);

    // The geography of these arguments, built once and shared while a Heightfield holds it (the
    // two last ones are kept a little longer): the same object for the same seed, parameters
    // and world, so that the many Heightfields of a process build it once. Thread-safe.
    [[nodiscard]] static std::shared_ptr<const Geography> shared(std::uint64_t seed,
                                                                 const GeographyParams& params,
                                                                 const WorldSize& world);

    // At (x, z) in the canonical range of the world (as Heightfield::sampleAt brings them).
    [[nodiscard]] GeographySample sampleAt(double x, double z) const;
    // GeographySample::land alone at (x, z), without the coast (cheaper).
    [[nodiscard]] float landAt(double x, double z) const;

    [[nodiscard]] const GeographyParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }
    [[nodiscard]] int columns() const { return columns_; }
    [[nodiscard]] int rows() const { return rows_; }
    // Shift of the map along x: the point x of the world reads the map at x + anchorX.
    [[nodiscard]] double anchorX() const { return anchorX_; }
    // Field f of node (i, j) of the map (indices taken modulo the counts).
    [[nodiscard]] float node(int i, int j, Field f) const;
    // The drainage of node (i, j) of the map (indices taken modulo the counts): the step to the
    // node downstream, (di, dj) with each in {-1, 0, 1}, false at sea; and the area it drains,
    // square meters (0 at sea).
    [[nodiscard]] bool downstream(int i, int j, int& di, int& dj) const;
    [[nodiscard]] float drainedArea(int i, int j) const;
    // The crust stream chosen and the land masses of at least continentArea it gives.
    [[nodiscard]] int crustCandidate() const { return candidate_; }
    [[nodiscard]] int continents() const { return continents_; }

    // 64-bit FNV-1a of the raw bits of every field of every node, of the drainage and of the
    // anchor.
    [[nodiscard]] std::uint64_t checksum() const;
    // Bytes held once built.
    [[nodiscard]] std::size_t memoryBytes() const;

    struct Noises; // the coast noises (geography.cpp)

private:
    // The coast signal at (x, z) from the fields of the map there.
    [[nodiscard]] double coastAt(double x, double z, const GeographySample& map) const;
    // The fields of the map alone at (geoX, z), geoX in the canonical range of the map.
    [[nodiscard]] GeographySample mapAt(double geoX, double z) const;
    [[nodiscard]] double chooseAnchor() const;
    // The land of the map sample s at (x, z) with the troughs of the rifts cut in it.
    [[nodiscard]] float riftLand(const GeographySample& s, double x, double z) const;
    // The fields of node (column, row), both in range.
    [[nodiscard]] const float* fieldsOf(int column, int row) const;
    // Marks the nodes whose spans of the map read a rift (rifts_).
    void markRifts();

    GeographyParams params_;
    WorldSize world_;
    int columns_ = 1;
    int rows_ = 1;
    double sizeX_ = 1.0;
    double sizeZ_ = 1.0;
    double anchorX_ = 0.0;
    int candidate_ = 0;
    int continents_ = 0;
    std::vector<float> nodes_; // kFields floats per node, row by row
    // 1 where the span of the map from a node reads a node with a rift (kRift or kRiftFloor not
    // 0): mapAt sums the fields of the rifts only there, the same bits for a fraction of the cost.
    std::vector<std::uint8_t> rifts_;
    // The drainage of the map: the step to the node downstream of each node (-1 at sea) and the
    // area it drains (square meters).
    std::vector<std::int8_t> down_;
    std::vector<float> drainedArea_;
    std::shared_ptr<const Noises> noises_;
};

} // namespace terrain
