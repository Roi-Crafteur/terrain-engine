#pragma once

// The runoff and its infiltration (task 11C, step 1): what part of the water runs off over the
// ground, and how many small streams that makes.
//
// The water the rivers carry over a year is the rain (or the melt) less what the land gives back
// to the air: the yield of the planet level, P - AET (planet_climate.hpp, Turc-Pike), unchanged.
// How it gets there is decided here:
// - the surface share s runs off over the ground at once: in a sheet down the slopes, then
//   gathered in rills, ravines and streams. It grows where the rain is stronger than what the
//   ground takes in: an impermeable rock or soil (crystalline cores, clays and marls), steep
//   slopes, bare ground (the forest takes the water in: roots, litter), frozen ground, storms
//   (the convective rains of the tropics, the rare violent rains of the deserts);
// - the rest, 1 - s, sinks in and comes out at the springs: the base flow that keeps a stream
//   wet between the rains.
// The surface water cuts the channels: a channel starts where the area that drains to a point,
// times the slope there, times the surface water of a year per square meter, passes a threshold
// (the area-slope law of the channel heads, Montgomery and Dietrich), within bounds: on an
// impermeable steep slope of a wet climate the heads lie a few hundredths of a square kilometer
// apart, on a permeable plain of a dry one tens of square kilometers. The base flow decides
// where a channel holds water all year (a brook), and where it is dry between the storms (a
// ravine, a wadi). No floods, no low waters, no seasons: the water stays at its mean.
//
// The permeability of the ground is the geology's, never the biome's: the crystalline cores of the
// mountain belts and the old massifs hold little water, the sediments of the plains and basins are
// clays, marls, sandstones or limestones by a slow noise of the lithology, the troughs of the rifts
// are filled with sediments, the sand of the ergs takes in all. The provinces of the karst and of
// the volcanoes (agents 46 and 45) come in here when they are on main.
//
// Pure functions of their inputs, deterministic, thread-safe.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <cstdint>

namespace terrain {

struct RunoffParams {
    // The permeability of the ground, in [0, 1] (0: none of the rain sinks in): of the
    // crystalline cores under the mountain belts, of the old worn massifs, of the sediments of
    // the plains from clays to limestones (the lithology noise), of the fill of the rifts and of
    // the sand of the ergs.
    float crystalline = 0.12f;
    float oldMassif = 0.22f;
    float clay = 0.06f;
    float limestone = 0.8f;
    float riftFill = 0.45f;
    float sand = 0.92f;
    // The lithology of the sediments: a slow noise of this wavelength (meters), from clay (-1)
    // to limestone (+1) through marls and sandstones.
    double lithologyWavelength = 350'000.0;
    // The slope at which the share of the slope term is half of its most (meters per meter).
    float slopeHalf = 0.15f;
    // The surface share never falls below this (the rain on the channels, the saturated ground
    // along them).
    float leastSurface = 0.05f;
    // The channel heads: area x slope x surface water a year (m^2 x m/m x m/yr) of headFlux
    // starts a channel, its area within [leastHead, mostHead + permeability x permeableHead]
    // square meters; the slope read at least headSlope; the rain read at least stormRain mm a
    // year (the rare violent rains of the deserts cut wadis).
    double headFlux = 7500.0;
    double leastHead = 5.0e4;
    double mostHead = 3.0e5;
    double permeableHead = 5.0e6;
    double headSlope = 0.01;
    double stormRain = 150.0;
    // A channel holds water all year where its base flow reaches wetFlow m^3/s, or its whole
    // flow wetTotal.
    double wetFlow = 0.002;
    double wetTotal = 0.01;
};

// The climate and the ground the partition reads at a point.
struct RunoffSite {
    float precipitation = 0.0f; // mm/yr
    float pet = 0.0f;           // potential evapotranspiration, mm/yr
    float temperature = 0.0f;   // mean annual, degrees Celsius
    float latitude = 0.0f;      // degrees from the equator, either side
    float permeability = 0.0f;  // [0, 1]
    float slope = 0.0f;         // meters per meter, at the scale of the streams
};

// What the partition makes of it.
struct RunoffShare {
    float vegetation = 0.0f; // [0, 1], from bare ground to a closed forest (the climate's)
    float storm = 0.0f;      // [0, 1], how violent the rains are
    float frost = 0.0f;      // [0, 1], how much of the year the ground is frozen
    float surface = 0.0f;    // [0, 1], the share of the yield that runs off over the ground
    double headArea = 0.0;   // m^2: the area a channel starts from there
};

[[nodiscard]] RunoffShare partitionRunoff(const RunoffSite& site, const RunoffParams& params);

// Whether a channel of discharge flow (m^3/s), of surface share surface, holds water all year.
[[nodiscard]] bool channelWet(double flow, float surface, const RunoffParams& params);

// The permeability of the ground of a world, from its geology.
class Permeability {
public:
    Permeability(std::uint64_t seed, const WorldSize& world, const RunoffParams& params = {});
    // At the canonical (x, z), over the relief sample s there (Heightfield::sampleAt of the
    // relief without the water): its mountain mask, its intensity, the rifts and the ergs; and
    // character, how worn the relief is (GeographySample::character, 0.5 without the geography).
    [[nodiscard]] float at(double x, double z, const HeightSample& s, float character) const;
    // The lithology of the sediments there, [-1, 1] (clay to limestone).
    [[nodiscard]] float lithology(double x, double z) const;

private:
    RunoffParams params_;
    noise::FbmOctaves lithology_;
};

} // namespace terrain
