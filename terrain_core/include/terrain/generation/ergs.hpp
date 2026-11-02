#pragma once

// The seas of dunes (ergs, phase 12): a province of the relief in the heart of the dry
// continents, placed by the climate and the relief of the planet level, never by the biome (the
// desert biome follows from the same climate, not the other way round).
//
// The ergs are laid after the planet level, like the valleys of the rivers (Heightfield::
// withHydrology): relief -> planet level -> dunes -> valleys. The planet level is built from the
// relief without dunes, so its checksum (and the planet file) does not depend on them, and the
// courses of the rivers are found on the relief without dunes too.
//
// Where an erg lies (the mask, in [0, 1], never a strict zone):
//   - a dry climate: the humidity P / (P + PET) of the planet level well below that of a steppe;
//   - warm: no erg on frozen or cold ground;
//   - far from the sea: the heart of a continent;
//   - low relief: plains, basins and soft plateaus, never under the mountains;
//   - no water: no dune within the corridor of a river carved by the hydrology, nor on a lake or
//     a depression of the relief, the dunes going down gently toward their edges.
// The climate is read on blocks of 8 x 8 cells of the planet level, blurred along the meridians
// over a few degrees of latitude and warped by a slow noise, so that the sharp lines of the
// climate of the planet level (its bands of winds) never show in the ergs.
//
// The rule of the wadis (for the river network, task 11C): a river of the planet level carved by
// the hydrology (discharge at least ErgParams::minDischarge) crosses an erg in a corridor free of
// dunes: no dune within corridorClear meters of its cells, full dunes beyond corridorClear +
// corridorFade. A smaller stream is not carved: it stops at the foot of the erg. The lakes of the
// planet level (terminal and salt lakes included) take the same corridor, as do the depressions
// below the sea level.
//
// Where the mask is 0 the dunes are exactly 0: the world keeps every bit of its relief there.
// Deterministic, thread-safe, periodic on the world.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain {

class PlanetLevel;

struct ErgParams {
    bool enabled = true;
    // The humidity of the climate (PlanetLevel::humidityAt): a full erg at or below arid, none
    // at or above dry.
    float aridHumidity = 0.15f;
    float dryHumidity = 0.32f;
    // The humidity at the point itself (PlanetLevel::humidityAt, bilinear): the erg fades out from
    // humidLow to humidHigh, so that the blurred climate never brings one onto a humid land. Above
    // the semi-arid lands, away from the sharp lines of the bands of the planet level.
    float humidLow = 0.42f;
    float humidHigh = 0.55f;
    // Mean annual temperature of the land, Celsius: none at or below cold, full at warm.
    float coldTemperature = 6.0f;
    float warmTemperature = 14.0f;
    // Distance to the sea, meters: none within coastNear, full beyond coastFar.
    double coastNear = 120'000.0;
    double coastFar = 400'000.0;
    // The climate blurred along the meridians over this many degrees of latitude (two box
    // passes of this radius) and across them over a block.
    double blurDegrees = 4.0;
    // The slow warp of the field, degrees of latitude, and its wavelength.
    double warpDegrees = 5.0;
    double warpWavelength = 2'000'000.0;
    // The supply of sand: a slow noise of this wavelength, none of it at or below supplyLow, all
    // at supplyHigh. The ergs are patches within the dry lands, never bands of latitude.
    double supplyWavelength = 900'000.0;
    float supplyLow = -0.2f;
    float supplyHigh = 0.25f;
    // The edges: a noise of this wavelength frays the mask where it passes its threshold.
    double edgeWavelength = 70'000.0;
    float edgeNoise = 0.12f;
    // The relief at the point (HeightSample::relief): full dunes at or below low, none at high.
    float reliefLow = 0.10f;
    float reliefHigh = 0.28f;
    // The corridors of the water (above), meters from the course of a river (the polyline of the
    // centers of its cells) or from the square of a lake cell.
    double corridorClear = 5'000.0;
    double corridorFade = 8'000.0;
    double minDischarge = 10.0; // m^3/s: the rivers the hydrology carves (HydrologyParams)
};

// What the climate and the place make of a point (Ergs::at).
struct ErgPoint {
    float mask = 0.0f;        // [0, 1]: how much of an erg the point is
    float sand = 0.0f;        // [0, 1]: from scattered barchans to the draas of the heart
    float variability = 0.0f; // [0, 1]: from a steady wind to winds from every side
    double windX = 1.0;       // unit direction toward which the sand moves (+x east, +z south)
    double windZ = 0.0;
};

// The kinds of dunes, for the maps and the tests.
enum class DuneKind : std::uint8_t { None, Barchan, Transverse, Seif, Star, Draa };

class Ergs {
public:
    // The ergs of the planet level of a world, seed drawing their dunes. depressions: those of the
    // relief (Heightfield::depressions). The planet level must outlive the ergs (they read its
    // humidity). Throws std::invalid_argument for an empty planet grid.
    Ergs(std::uint64_t seed, const PlanetLevel& planet, const std::vector<Depression>& depressions,
         const ErgParams& params = {});
    ~Ergs();
    Ergs(const Ergs&) = delete;
    Ergs& operator=(const Ergs&) = delete;

    // False where no erg can lie within reach of the canonical (x, z): the quick test before
    // anything else (one block of the field read).
    [[nodiscard]] bool mayHold(double x, double z) const;
    // The potential of the climate at the canonical (x, z), [0, 1]: the blocks of the field,
    // smoothly interpolated and warped, before the edges, the relief and the water.
    [[nodiscard]] float climateAt(double x, double z) const;
    // The erg at the canonical (x, z) over a relief of intensity relief (HeightSample::relief).
    [[nodiscard]] ErgPoint at(double x, double z, float relief) const;
    // The meters of sand the dunes lay over the relief of s at the canonical (x, z): exactly 0
    // where the mask is 0 (and over the sea).
    [[nodiscard]] float dunesAt(double x, double z, const HeightSample& s) const;
    // The dunes of an erg point, and the kind that weighs most there.
    [[nodiscard]] float dunesOf(double x, double z, const ErgPoint& e) const;
    [[nodiscard]] DuneKind kindOf(const ErgPoint& e) const;

    // The prevailing wind at the canonical (x, z): the bands of the planet level, smoothly
    // joined, turned by a slow noise. Unit vector.
    [[nodiscard]] std::array<double, 2> windAt(double x, double z) const;

    [[nodiscard]] const ErgParams& params() const;
    [[nodiscard]] const PlanetGrid& grid() const;
    // The bytes held: the blocks of the field and their water.
    [[nodiscard]] std::size_t memoryBytes() const;

    struct Impl; // ergs_impl.hpp

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace terrain
