#include "terrain/dev/json_value.hpp"

#include "json_number.hpp"

#include <array>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace terrain::dev {

namespace {

// The C library reads and writes numbers with the decimal point of the current locale: the
// JSON point becomes it before strtod, and it becomes a point again after snprintf.
char localePoint() {
    const std::lconv* conventions = std::localeconv();
    return conventions != nullptr && conventions->decimal_point != nullptr &&
                   conventions->decimal_point[0] != '\0'
               ? conventions->decimal_point[0]
               : '.';
}

} // namespace

double readJsonNumber(std::string token) {
    const char point = localePoint();
    for (char& c : token) {
        if (c == '.') {
            c = point;
        }
    }
    return std::strtod(token.c_str(), nullptr);
}

namespace {

std::string printed(double value, int digits) {
    std::array<char, 40> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*g", digits, value);
    std::string text(buffer.data());
    const char point = localePoint();
    for (char& c : text) {
        if (c == point) {
            c = '.';
        }
    }
    return text;
}

void writeTo(const JsonValue& value, std::string& out) {
    switch (value.kind) {
    case JsonKind::Null: out += "null"; break;
    case JsonKind::Bool: out += value.boolean ? "true" : "false"; break;
    case JsonKind::Number: out += formatJsonNumber(value.number); break;
    case JsonKind::String: out += quoteJson(value.text); break;
    case JsonKind::Array:
        out += '[';
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            out += i == 0 ? "" : ", ";
            writeTo(value.items[i], out);
        }
        out += ']';
        break;
    case JsonKind::Object:
        out += '{';
        for (std::size_t i = 0; i < value.members.size(); ++i) {
            out += i == 0 ? "" : ", ";
            out += quoteJson(value.members[i].key);
            out += ": ";
            writeTo(value.members[i].value, out);
        }
        out += '}';
        break;
    }
}

} // namespace

JsonValue JsonValue::makeBool(bool value) {
    JsonValue v;
    v.kind = JsonKind::Bool;
    v.boolean = value;
    return v;
}

JsonValue JsonValue::makeNumber(double value) {
    JsonValue v;
    v.kind = JsonKind::Number;
    v.number = value;
    return v;
}

JsonValue JsonValue::makeString(std::string value) {
    JsonValue v;
    v.kind = JsonKind::String;
    v.text = std::move(value);
    return v;
}

JsonValue JsonValue::makeArray() {
    JsonValue v;
    v.kind = JsonKind::Array;
    return v;
}

JsonValue JsonValue::makeArray(std::vector<JsonValue> values) {
    JsonValue v;
    v.kind = JsonKind::Array;
    v.items = std::move(values);
    return v;
}

JsonValue JsonValue::makeObject() {
    JsonValue v;
    v.kind = JsonKind::Object;
    return v;
}

JsonValue JsonValue::makeObject(std::vector<JsonMember> values) {
    JsonValue v;
    v.kind = JsonKind::Object;
    v.members = std::move(values);
    return v;
}

const JsonValue* JsonValue::find(std::string_view key) const {
    if (kind != JsonKind::Object) {
        return nullptr;
    }
    for (const JsonMember& member : members) {
        if (member.key == key) {
            return &member.value;
        }
    }
    return nullptr;
}

bool operator==(const JsonValue& a, const JsonValue& b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
    case JsonKind::Null: return true;
    case JsonKind::Bool: return a.boolean == b.boolean;
    case JsonKind::Number: return a.number == b.number;
    case JsonKind::String: return a.text == b.text;
    case JsonKind::Array: return a.items == b.items;
    case JsonKind::Object: return a.members == b.members;
    }
    return false;
}

bool operator==(const JsonMember& a, const JsonMember& b) {
    return a.key == b.key && a.value == b.value;
}

JsonError::JsonError(std::size_t line, std::size_t column, const std::string& message)
    : std::runtime_error("line " + std::to_string(line) + ", column " + std::to_string(column) +
                         ": " + message),
      line_(line), column_(column) {}

std::string writeJson(const JsonValue& value) {
    std::string out;
    writeTo(value, out);
    return out;
}

std::string quoteJson(std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (byte < 0x20) {
            out += "\\u00";
            out += kHex[byte >> 4];
            out += kHex[byte & 0x0F];
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

std::string formatJsonNumber(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("JSON has no number for NaN nor the infinities");
    }
    // The shortest of the forms of 1 to 17 significant digits that reads back to the bits.
    std::string text = printed(value, 17);
    for (int digits = 1; digits < 17; ++digits) {
        std::string shorter = printed(value, digits);
        const double back = readJsonNumber(shorter);
        if (back == value && std::signbit(back) == std::signbit(value)) {
            text = std::move(shorter);
            break;
        }
    }
    // Whole numbers below 10^17 in full ("30", not "3e+01"): %g writes without an exponent
    // when it is below the number of digits asked for.
    const std::size_t e = text.find('e');
    if (e != std::string::npos && text[e + 1] == '+') {
        const int exponent = std::atoi(text.c_str() + e + 2);
        if (exponent < 17) {
            text = printed(value, exponent + 1);
        }
    }
    return text;
}

} // namespace terrain::dev
