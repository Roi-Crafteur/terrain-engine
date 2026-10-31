#pragma once

// Internal to the settlement: the members of an object of a data file read with clear errors
// ("<file>: <path>: <reason> (line L, column C)"), and the shared pieces of the files
// (trapezoids, ramps, preferences, ranges).

#include "terrain/generation/settlement/preference.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/json/json.hpp"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terrain::settlement {

class Fields {
public:
    // value must be an object.
    Fields(const json::Value& value, std::string source, std::string path);

    [[nodiscard]] const json::Value& value() const { return *value_; }
    [[nodiscard]] const std::string& source() const { return source_; }
    [[nodiscard]] const std::string& path() const { return path_; }
    [[nodiscard]] std::string pathOf(std::string_view key) const;

    [[nodiscard]] bool has(std::string_view key) const { return value_->find(key) != nullptr; }
    // A required member.
    [[nodiscard]] const json::Value& member(std::string_view key) const;

    [[nodiscard]] double number(std::string_view key) const;
    [[nodiscard]] double number(std::string_view key, double fallback) const;
    // A number in [low, high].
    [[nodiscard]] double numberIn(std::string_view key, double low, double high) const;
    [[nodiscard]] double numberIn(std::string_view key, double low, double high,
                                  double fallback) const;
    // A whole number in [low, high].
    [[nodiscard]] int integer(std::string_view key, int low, int high) const;
    [[nodiscard]] std::string text(std::string_view key) const;
    [[nodiscard]] std::string text(std::string_view key, std::string_view fallback) const;
    [[nodiscard]] bool flag(std::string_view key, bool fallback) const;
    // An array of strings, empty when absent.
    [[nodiscard]] std::vector<std::string> strings(std::string_view key) const;

    [[nodiscard]] Fields object(std::string_view key) const;
    [[nodiscard]] std::optional<Fields> optionalObject(std::string_view key) const;

    // Refuses any member whose key is not one of allowed.
    void only(std::initializer_list<std::string_view> allowed) const;

    [[noreturn]] void fail(const json::Value& at, std::string_view key,
                           const std::string& reason) const;
    [[noreturn]] void fail(const std::string& reason) const;

private:
    const json::Value* value_;
    std::string source_;
    std::string path_;
};

// {"min", "max"} of numbers in [low, high], min <= max.
[[nodiscard]] RealRange readRealRange(const Fields& parent, std::string_view key, double low,
                                      double high);
[[nodiscard]] CountRange readCountRange(const Fields& parent, std::string_view key, int low,
                                        int high);
// A trapezoid: min, idealMin, idealMax, max (or ideal for both ideals), optional hardMin,
// hardMax, floor, over fallback (the trapezoid of a base model) when given: every field read
// replaces it.
[[nodiscard]] Trapezoid readTrapezoid(const Fields& f, const Trapezoid* fallback = nullptr);
[[nodiscard]] Ramp readRamp(const Fields& f);
// physicalPreferences (and physicalWeights) of a model or a people, criterion by criterion over
// base.
[[nodiscard]] Preferences readPreferences(const Fields& parent, const Preferences& base);

} // namespace terrain::settlement
