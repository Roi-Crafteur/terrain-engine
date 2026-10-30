#include "terrain/generation/settlement/preference.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace terrain::settlement {

namespace {

constexpr std::array<std::string_view, kCriteria> kNames = {
    "altitude", "temperature",   "rainfall",      "humidity",
    "roughness", "waterDistance", "coastDistance", "latitude"};

} // namespace

std::string_view criterionName(Criterion c) {
    return kNames[static_cast<std::size_t>(c)];
}

std::optional<Criterion> criterionNamed(std::string_view name) {
    for (std::size_t k = 0; k < kCriteria; ++k) {
        if (kNames[k] == name) {
            return static_cast<Criterion>(k);
        }
    }
    return std::nullopt;
}

double trapezoidScore(double value, const Trapezoid& t) {
    if (value < t.hardMin || value > t.hardMax) {
        return 0.0;
    }
    if (value >= t.idealMin && value <= t.idealMax) {
        return 1.0;
    }
    if (value <= t.min || value >= t.max) {
        return t.floor;
    }
    const double s = value < t.idealMin ? (value - t.min) / (t.idealMin - t.min)
                                        : (t.max - value) / (t.max - t.idealMax);
    return t.floor + (1.0 - t.floor) * s;
}

double rampScore(double value, const Ramp& r) {
    if (r.to == r.from) {
        return value >= r.to ? 1.0 : 0.0;
    }
    return std::clamp((value - r.from) / (r.to - r.from), 0.0, 1.0);
}

bool Preferences::empty() const {
    return std::none_of(has.begin(), has.end(), [](bool b) { return b; });
}

double physicalScore(const Preferences& p, const CriterionValues& values,
                     const PhysicalScoreRule& rule) {
    double weighted = 0.0;
    double weights = 0.0;
    double lowest = 1.0;
    bool any = false;
    for (std::size_t k = 0; k < kCriteria; ++k) {
        if (!p.has[k]) {
            continue;
        }
        const double s = trapezoidScore(values[k], p.range[k]);
        if (s == 0.0) {
            return 0.0;
        }
        any = true;
        weighted += p.weight[k] * s;
        weights += p.weight[k];
        lowest = std::min(lowest, s);
    }
    if (!any) {
        return 1.0;
    }
    const double mean = weights > 0.0 ? weighted / weights : lowest;
    return rule.meanShare * mean + rule.minShare * lowest;
}

std::uint8_t quantize(double score) {
    const double s = std::clamp(score, 0.0, 1.0);
    return static_cast<std::uint8_t>(s * 255.0 + 0.5);
}

} // namespace terrain::settlement
