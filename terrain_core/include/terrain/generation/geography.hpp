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
    float land = 1.0f;     // drained regional elevation of the land, >= 1 m, also over the sea
    float landDX = 0.0f;   // its gradient along x and z, meters per meter
    float landDZ = 0.0f;
    float sea = 0.0f;  // floor of the ocean, meters (the shelf depth under the land)
    float belt = 0.0f;   // [0, 1], the mountain belts of the collisions of the plates
    float hills = 0.0f;  // meters, the amplitude of the hills of the provinces
    float massif = 0.0f; // [0, 1], the old massifs and intraplate high relief of the provinces
};

// The share of the hills and of the mountains left by the valleys of the drainage of the map at a
// point: 1 far from them, less in them, down to 0 on the floors of the largest.
struct ValleyShape {
    float hills = 1.0f;
    float mountains = 1.0f; // steeper walls
};

// The planetary geography of a seed (phase 11R): a map of the whole world computed once,
// deterministic to the bit on every platform, then sampled at any point. Its continents do not
// follow the tectonic plates: a crust noise thresholded to a share of land, chosen among a few
// streams for its count of continents. Its regional elevation drains: it climbs from the coast
// at least GeographyParams::drainSlope along the way water takes to the sea, so the map holds no
// closed depression. Its tectonic plates raise mountain belts and high plateaus along their
// collisions (island arcs and trenches at sea), rifts and mid-ocean ridges along their
// divergences. The point (0, 0) lies on a coast of a continent, near a mountain belt when one
// comes close enough: the map is shifted along x to put one there.
class Geography {
public:
    // Fields of a node of the map, in this order.
    enum Field : std::size_t { kCoast, kLand, kSea, kBelt, kHills, kMassif, kFields };

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

    // The valleys of the drainage of the map at (x, z), canonical (GeographyParams): they follow
    // its tree toward the sea (each node to the node downstream), bent; the larger the area they
    // drain, the deeper they are and the wider their floors, flat and level across. A relief
    // scaled by it has no minimum but on these floors, which descend with the regional elevation
    // and step down where a valley joins a larger one: it drains.
    [[nodiscard]] ValleyShape valleyShape(double x, double z) const;

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
    // The fields of node (column, row), both in range.
    [[nodiscard]] const float* fieldsOf(int column, int row) const;

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
    // The drainage of the map: the step to the node downstream of each node (-1 at sea), the
    // area it drains (square meters), the polylines of its valleys (geography::valleyGeometry).
    std::vector<std::int8_t> down_;
    std::vector<float> drainedArea_;
    std::vector<float> valleyGeometry_;
    std::shared_ptr<const Noises> noises_;
};

} // namespace terrain
