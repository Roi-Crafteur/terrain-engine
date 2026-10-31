#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

// The scores of the settlement (docs/PEUPLEMENT.md, section 6): a value measured on the planet
// level scored by a trapezoid, the physical score of a set of preferences.
namespace terrain::settlement {

// What a preference, a terrain model or a rule of the analysis maps reads of a cell of the
// planet level, in these units.
enum class Criterion : std::uint8_t {
    Altitude,      // meters (the floor of the cell)
    Temperature,   // mean annual, degrees Celsius
    Rainfall,      // mm/yr
    Humidity,      // P / (P + PET), [0, 1) (PlanetLevel::humidityAt)
    Roughness,     // [0, 1], flat to very rugged within the cell of 8 km
    WaterDistance, // km to the nearest fresh water (river or lake that spills)
    CoastDistance, // km to the sea
    Latitude,      // degrees from the equator, 0 to 90
};
inline constexpr std::size_t kCriteria = 8;

[[nodiscard]] std::string_view criterionName(Criterion c);
[[nodiscard]] std::optional<Criterion> criterionNamed(std::string_view name);

// The values of the criteria at a cell, by Criterion.
using CriterionValues = std::array<double, kCriteria>;

inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

// A score in trapezoid (section 6): 1 over [idealMin, idealMax], down linearly to floor at min and
// max, floor between hardMin and min and between max and hardMax, 0 beyond the hard limits (an
// impossibility). hardMin <= min <= idealMin <= idealMax <= max <= hardMax.
struct Trapezoid {
    double hardMin = -kInfinity;
    double min = 0.0;
    double idealMin = 0.0;
    double idealMax = 0.0;
    double max = 0.0;
    double hardMax = kInfinity;
    double floor = 0.05;

    friend bool operator==(const Trapezoid&, const Trapezoid&) = default;
};
[[nodiscard]] double trapezoidScore(double value, const Trapezoid& t);

// A ramp from 0 at from to 1 at to (decreasing if to < from), clamped: a factor of danger or of
// an energy that only grows past a threshold.
struct Ramp {
    double from = 0.0;
    double to = 1.0;

    friend bool operator==(const Ramp&, const Ramp&) = default;
};
[[nodiscard]] double rampScore(double value, const Ramp& r);

// Physical preferences: an optional trapezoid and a weight per criterion.
struct Preferences {
    std::array<bool, kCriteria> has{};
    std::array<Trapezoid, kCriteria> range{};
    std::array<double, kCriteria> weight = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

    [[nodiscard]] bool empty() const;
    friend bool operator==(const Preferences&, const Preferences&) = default;
};

// The physical score (section 6): meanShare times the mean of the scores weighted by their
// weights plus minShare times the lowest score, so that one catastrophic criterion lowers a site
// a lot without cancelling it; 0 if a criterion is beyond a hard limit, 1 without any criterion.
struct PhysicalScoreRule {
    double meanShare = 0.7;
    double minShare = 0.3;

    friend bool operator==(const PhysicalScoreRule&, const PhysicalScoreRule&) = default;
};
[[nodiscard]] double physicalScore(const Preferences& p, const CriterionValues& values,
                                   const PhysicalScoreRule& rule);

// Bytes of a score in [0, 1] kept in the maps: round(255 s), and back.
[[nodiscard]] std::uint8_t quantize(double score);
[[nodiscard]] inline double unquantize(std::uint8_t q) { return q / 255.0; }

} // namespace terrain::settlement
