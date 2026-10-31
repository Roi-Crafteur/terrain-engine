#include "data_fields.hpp"

#include <cmath>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace terrain::settlement {

namespace {

const char* typeName(json::Type t) {
    switch (t) {
    case json::Type::Null:
        return "null";
    case json::Type::Boolean:
        return "a boolean";
    case json::Type::Number:
        return "a number";
    case json::Type::String:
        return "a string";
    case json::Type::Array:
        return "an array";
    case json::Type::Object:
        return "an object";
    }
    return "?";
}

std::string expected(const char* what, const json::Value& v) {
    return std::string("expected ") + what + ", found " + typeName(v.type());
}

} // namespace

Fields::Fields(const json::Value& value, std::string source, std::string path)
    : value_(&value), source_(std::move(source)), path_(std::move(path)) {
    if (!value.isObject()) {
        fail(value, "", expected("an object", value));
    }
}

std::string Fields::pathOf(std::string_view key) const {
    if (path_.empty()) {
        return std::string(key);
    }
    return key.empty() ? path_ : path_ + "." + std::string(key);
}

void Fields::fail(const json::Value& at, std::string_view key, const std::string& reason) const {
    std::ostringstream message;
    message << source_ << ": ";
    const std::string where = pathOf(key);
    if (!where.empty()) {
        message << where << ": ";
    }
    message << reason;
    if (at.line() > 0) {
        message << " (line " << at.line() << ", column " << at.column() << ')';
    }
    throw json::Error(message.str());
}

void Fields::fail(const std::string& reason) const {
    fail(*value_, "", reason);
}

const json::Value& Fields::member(std::string_view key) const {
    const json::Value* v = value_->find(key);
    if (v == nullptr) {
        fail(*value_, key, "missing");
    }
    return *v;
}

double Fields::number(std::string_view key) const {
    const json::Value& v = member(key);
    if (!v.isNumber()) {
        fail(v, key, expected("a number", v));
    }
    return v.asNumber();
}

double Fields::number(std::string_view key, double fallback) const {
    return has(key) ? number(key) : fallback;
}

double Fields::numberIn(std::string_view key, double low, double high) const {
    const double n = number(key);
    if (n < low || n > high) {
        std::ostringstream reason;
        reason << n << " is out of [" << low << ", " << high << ']';
        fail(member(key), key, reason.str());
    }
    return n;
}

double Fields::numberIn(std::string_view key, double low, double high, double fallback) const {
    return has(key) ? numberIn(key, low, high) : fallback;
}

int Fields::integer(std::string_view key, int low, int high) const {
    const double n = numberIn(key, low, high);
    if (n != std::floor(n)) {
        fail(member(key), key, "expected a whole number");
    }
    return static_cast<int>(n);
}

std::string Fields::text(std::string_view key) const {
    const json::Value& v = member(key);
    if (!v.isString()) {
        fail(v, key, expected("a string", v));
    }
    return v.asString();
}

std::string Fields::text(std::string_view key, std::string_view fallback) const {
    return has(key) ? text(key) : std::string(fallback);
}

bool Fields::flag(std::string_view key, bool fallback) const {
    if (!has(key)) {
        return fallback;
    }
    const json::Value& v = member(key);
    if (!v.isBoolean()) {
        fail(v, key, expected("a boolean", v));
    }
    return v.asBoolean();
}

std::vector<std::string> Fields::strings(std::string_view key) const {
    std::vector<std::string> out;
    if (!has(key)) {
        return out;
    }
    const json::Value& v = member(key);
    if (!v.isArray()) {
        fail(v, key, expected("an array", v));
    }
    for (const json::Value& item : v.items()) {
        if (!item.isString()) {
            fail(item, key, expected("an array of strings", item));
        }
        out.push_back(item.asString());
    }
    return out;
}

Fields Fields::object(std::string_view key) const {
    return Fields(member(key), source_, pathOf(key));
}

std::optional<Fields> Fields::optionalObject(std::string_view key) const {
    if (!has(key)) {
        return std::nullopt;
    }
    return object(key);
}

void Fields::only(std::initializer_list<std::string_view> allowed) const {
    for (std::size_t k = 0; k < value_->keys().size(); ++k) {
        const std::string& key = value_->keys()[k];
        bool known = false;
        for (const std::string_view a : allowed) {
            known = known || a == key;
        }
        if (!known) {
            std::string list;
            for (const std::string_view a : allowed) {
                list += list.empty() ? "" : ", ";
                list += a;
            }
            fail(value_->items()[k], key, "unknown key (expected one of: " + list + ")");
        }
    }
}

RealRange readRealRange(const Fields& parent, std::string_view key, double low, double high) {
    const Fields f = parent.object(key);
    f.only({"min", "max"});
    const RealRange r{f.numberIn("min", low, high), f.numberIn("max", low, high)};
    if (r.min > r.max) {
        f.fail("min is above max");
    }
    return r;
}

CountRange readCountRange(const Fields& parent, std::string_view key, int low, int high) {
    const Fields f = parent.object(key);
    f.only({"min", "max"});
    const CountRange r{f.integer("min", low, high), f.integer("max", low, high)};
    if (r.min > r.max) {
        f.fail("min is above max");
    }
    return r;
}

Trapezoid readTrapezoid(const Fields& f, const Trapezoid* fallback) {
    f.only({"hardMin", "min", "ideal", "idealMin", "idealMax", "max", "hardMax", "floor",
            "weight"});
    if (f.has("ideal") && (f.has("idealMin") || f.has("idealMax"))) {
        f.fail("ideal and idealMin / idealMax together");
    }
    const auto get = [&](std::string_view key, const double* base) {
        if (f.has(key)) {
            return f.number(key);
        }
        if (key.starts_with("ideal") && f.has("ideal")) {
            return f.number("ideal");
        }
        if (base == nullptr) {
            f.fail("missing " + std::string(key));
        }
        return *base;
    };
    const Trapezoid* b = fallback;
    Trapezoid t;
    t.min = get("min", b != nullptr ? &b->min : nullptr);
    t.idealMin = get("idealMin", b != nullptr ? &b->idealMin : nullptr);
    t.idealMax = get("idealMax", b != nullptr ? &b->idealMax : nullptr);
    t.max = get("max", b != nullptr ? &b->max : nullptr);
    t.hardMin = f.number("hardMin", b != nullptr ? b->hardMin : -kInfinity);
    t.hardMax = f.number("hardMax", b != nullptr ? b->hardMax : kInfinity);
    t.floor = f.numberIn("floor", 0.0, 1.0, b != nullptr ? b->floor : 0.05);
    if (!(t.hardMin <= t.min && t.min <= t.idealMin && t.idealMin <= t.idealMax &&
          t.idealMax <= t.max && t.max <= t.hardMax)) {
        f.fail("expected hardMin <= min <= idealMin <= idealMax <= max <= hardMax");
    }
    return t;
}

Ramp readRamp(const Fields& f) {
    f.only({"from", "to"});
    return Ramp{f.number("from"), f.number("to")};
}

Preferences readPreferences(const Fields& parent, const Preferences& base) {
    Preferences p = base;
    if (const std::optional<Fields> prefs = parent.optionalObject("physicalPreferences")) {
        const json::Value& v = prefs->value();
        for (std::size_t k = 0; k < v.keys().size(); ++k) {
            const std::optional<Criterion> c = criterionNamed(v.keys()[k]);
            if (!c) {
                prefs->fail(v.items()[k], v.keys()[k], "unknown criterion");
            }
            const auto i = static_cast<std::size_t>(*c);
            const Fields f = prefs->object(v.keys()[k]);
            p.range[i] = readTrapezoid(f, p.has[i] ? &p.range[i] : nullptr);
            p.has[i] = true;
            p.weight[i] = f.numberIn("weight", 0.0, 100.0, p.weight[i]);
        }
    }
    if (const std::optional<Fields> weights = parent.optionalObject("physicalWeights")) {
        const json::Value& v = weights->value();
        for (std::size_t k = 0; k < v.keys().size(); ++k) {
            const std::optional<Criterion> c = criterionNamed(v.keys()[k]);
            if (!c) {
                weights->fail(v.items()[k], v.keys()[k], "unknown criterion");
            }
            p.weight[static_cast<std::size_t>(*c)] = weights->numberIn(v.keys()[k], 0.0, 100.0);
        }
    }
    return p;
}

} // namespace terrain::settlement
