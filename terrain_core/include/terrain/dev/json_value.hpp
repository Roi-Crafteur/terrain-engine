#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// A strict JSON reader and writer (RFC 8259) for the files of the developers, such as the places
// of data/dev/places.json (places_file.hpp): nothing of the world is read or written with it.
// Strict: no comment, no trailing comma, no NaN nor infinity, no leading zero, no control
// character nor invalid UTF-8 in a string, no duplicate key in an object, nothing after the
// value; a UTF-8 byte order mark at the start is skipped. Numbers are doubles: written in their
// shortest form that reads back to the same bits, so a value survives any number of round trips.
namespace terrain::dev {

enum class JsonKind : std::uint8_t { Null, Bool, Number, String, Array, Object };

struct JsonMember;

// A JSON value. Only the fields of its kind are meaningful; the members of an object keep the
// order of the file.
struct JsonValue {
    JsonKind kind = JsonKind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text; // String: UTF-8
    std::vector<JsonValue> items;
    std::vector<JsonMember> members;

    [[nodiscard]] static JsonValue makeBool(bool value);
    [[nodiscard]] static JsonValue makeNumber(double value);
    [[nodiscard]] static JsonValue makeString(std::string value);
    // Empty, or of these values (no default argument: JsonMember is not complete here).
    [[nodiscard]] static JsonValue makeArray();
    [[nodiscard]] static JsonValue makeArray(std::vector<JsonValue> values);
    [[nodiscard]] static JsonValue makeObject();
    [[nodiscard]] static JsonValue makeObject(std::vector<JsonMember> values);

    // The member of an object under key, or nullptr (also for a value of another kind).
    [[nodiscard]] const JsonValue* find(std::string_view key) const;
};

struct JsonMember {
    std::string key;
    JsonValue value;
};

// Same kind and same content, members in the same order; numbers compared by value (0 == -0).
[[nodiscard]] bool operator==(const JsonValue& a, const JsonValue& b);
[[nodiscard]] bool operator==(const JsonMember& a, const JsonMember& b);

// Why a text is not strict JSON: the message gives the line and column (from 1) where the
// reader stopped, for example "line 12, column 5: expected ',' or '}'".
class JsonError : public std::runtime_error {
public:
    JsonError(std::size_t line, std::size_t column, const std::string& message);

    [[nodiscard]] std::size_t line() const { return line_; }
    [[nodiscard]] std::size_t column() const { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

// Values nested deeper than this are refused (a malicious file never exhausts the stack).
inline constexpr int kJsonMaxDepth = 64;

// The value of a strict JSON text. Throws JsonError.
[[nodiscard]] JsonValue parseJson(std::string_view text);

// The value on one line: ", " between items and members, ": " after keys, strings escaped
// (", \, control characters; the rest of UTF-8 as it is). Throws std::invalid_argument for a
// number that is not finite.
[[nodiscard]] std::string writeJson(const JsonValue& value);

// A string as a JSON string literal, quotes included.
[[nodiscard]] std::string quoteJson(std::string_view text);

// The shortest decimal form of a finite double that reads back to the same bits, in JSON
// syntax: "182.03", "-6185104", "1e-07", "-0". Throws std::invalid_argument if not finite.
[[nodiscard]] std::string formatJsonNumber(double value);

} // namespace terrain::dev
