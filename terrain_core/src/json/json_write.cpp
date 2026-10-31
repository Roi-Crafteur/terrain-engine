#include "terrain/json/json.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace terrain::json {

Value Value::boolean(bool b) {
    Value v;
    v.type_ = Type::Boolean;
    v.boolean_ = b;
    return v;
}

Value Value::number(double n) {
    Value v;
    v.type_ = Type::Number;
    v.number_ = n;
    return v;
}

Value Value::string(std::string s) {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(s);
    return v;
}

Value Value::array() {
    Value v;
    v.type_ = Type::Array;
    return v;
}

Value Value::object() {
    Value v;
    v.type_ = Type::Object;
    return v;
}

const Value* Value::find(std::string_view key) const {
    for (std::size_t k = 0; k < keys_.size(); ++k) {
        if (keys_[k] == key) {
            return &items_[k];
        }
    }
    return nullptr;
}

void Value::push(Value v) {
    items_.push_back(std::move(v));
}

void Value::set(std::string key, Value v) {
    for (std::size_t k = 0; k < keys_.size(); ++k) {
        if (keys_[k] == key) {
            items_[k] = std::move(v);
            return;
        }
    }
    keys_.push_back(std::move(key));
    items_.push_back(std::move(v));
}

bool operator==(const Value& a, const Value& b) {
    if (a.type_ != b.type_) {
        return false;
    }
    switch (a.type_) {
    case Type::Null:
        return true;
    case Type::Boolean:
        return a.boolean_ == b.boolean_;
    case Type::Number:
        return std::bit_cast<std::uint64_t>(a.number_) == std::bit_cast<std::uint64_t>(b.number_);
    case Type::String:
        return a.string_ == b.string_;
    case Type::Array:
    case Type::Object:
        return a.keys_ == b.keys_ && a.items_ == b.items_;
    }
    return false;
}

namespace {

constexpr double kMaxExact = 9007199254740992.0; // 2^53
constexpr int kMaxFractionDigits = 22;
constexpr int kMaxDecimalExponent = 308;

// 10^0 to 10^22, exact.
double powerOfTen(int k) {
    double p = 1.0;
    for (int i = 0; i < k; ++i) {
        p *= 10.0;
    }
    return p;
}

void appendInteger(std::string& out, std::uint64_t m) {
    char digits[24];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + static_cast<int>(m % 10u));
        m /= 10u;
    } while (m != 0);
    while (n > 0) {
        out += digits[--n];
    }
}

// m 10^-k written with a decimal point.
void appendFraction(std::string& out, std::uint64_t m, int k) {
    std::string digits;
    appendInteger(digits, m);
    while (static_cast<int>(digits.size()) <= k) {
        digits.insert(digits.begin(), '0');
    }
    const std::size_t point = digits.size() - static_cast<std::size_t>(k);
    out.append(digits, 0, point);
    out += '.';
    out.append(digits, point, std::string::npos);
}

bool roundsBack(std::uint64_t m, int e, double a) {
    double back = 0.0;
    return decimalToDouble(m, e, false, back) &&
           std::bit_cast<std::uint64_t>(back) == std::bit_cast<std::uint64_t>(a);
}

void appendNumber(std::string& out, double v) {
    if (!std::isfinite(v)) {
        throw Error("cannot write a number that is not finite");
    }
    if (std::signbit(v)) {
        out += '-';
        v = -v;
    }
    if (v <= kMaxExact && v == std::floor(v)) {
        appendInteger(out, static_cast<std::uint64_t>(v));
        return;
    }
    for (int k = 1; k <= kMaxFractionDigits; ++k) {
        const double scaled = v * powerOfTen(k);
        if (scaled > kMaxExact) {
            break;
        }
        const auto m = static_cast<std::uint64_t>(scaled + 0.5);
        if (roundsBack(m, -k, v)) {
            appendFraction(out, m, k);
            return;
        }
    }
    // Above 2^53: m e k with the largest k, so the shortest m (10^k may be inexact past 10^22:
    // roundsBack checks every candidate).
    std::uint64_t best = 0;
    int bestK = 0;
    double p = 1.0;
    for (int k = 1; k <= kMaxDecimalExponent && v > kMaxExact; ++k) {
        p *= 10.0;
        const double scaled = v / p;
        if (scaled < 1.0) {
            break;
        }
        const auto m = static_cast<std::uint64_t>(scaled + 0.5);
        if (scaled <= kMaxExact && roundsBack(m, k, v)) {
            best = m;
            bestK = k;
        }
    }
    if (bestK == 0) {
        throw Error("cannot write a number without an exact short decimal");
    }
    appendInteger(out, best);
    out += 'e';
    appendInteger(out, static_cast<std::uint64_t>(bestK));
}

void appendString(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            if (u < 0x20u) {
                constexpr std::string_view kHex = "0123456789abcdef";
                out += "\\u00";
                out += kHex[u >> 4];
                out += kHex[u & 0x0Fu];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void newline(std::string& out, bool pretty, int depth) {
    if (pretty) {
        out += '\n';
        out.append(static_cast<std::size_t>(2 * depth), ' ');
    }
}

void appendValue(std::string& out, const Value& v, bool pretty, int depth) {
    switch (v.type()) {
    case Type::Null:
        out += "null";
        return;
    case Type::Boolean:
        out += v.asBoolean() ? "true" : "false";
        return;
    case Type::Number:
        appendNumber(out, v.asNumber());
        return;
    case Type::String:
        appendString(out, v.asString());
        return;
    case Type::Array:
    case Type::Object:
        break;
    }
    const bool object = v.isObject();
    out += object ? '{' : '[';
    for (std::size_t k = 0; k < v.size(); ++k) {
        newline(out, pretty, depth + 1);
        if (object) {
            appendString(out, v.keys()[k]);
            out += pretty ? ": " : ":";
        }
        appendValue(out, v.items()[k], pretty, depth + 1);
        if (k + 1 < v.size()) {
            out += ',';
        }
    }
    if (v.size() > 0) {
        newline(out, pretty, depth);
    }
    out += object ? '}' : ']';
}

} // namespace

std::string write(const Value& v, bool pretty) {
    std::string out;
    appendValue(out, v, pretty, 0);
    if (pretty) {
        out += '\n';
    }
    return out;
}

std::uint64_t fnv1a64(std::string_view bytes) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : bytes) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001B3ULL;
    }
    return h;
}

} // namespace terrain::json
