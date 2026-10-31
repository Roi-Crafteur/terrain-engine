#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The data files of the generator (the peoples, the resources and the configuration of the
// settlement, docs/PEUPLEMENT.md) in JSON, read strictly and the same on every platform: the
// numbers are decoded without the floating-point parsing of the standard library, so a file
// gives the same bits everywhere, and written back in a canonical form.
namespace terrain::json {

enum class Type : std::uint8_t { Null, Boolean, Number, String, Array, Object };

// A JSON text that cannot be read, or contents that do not fit what the reader expects: the
// message says where (the source, its line and column) and why.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A JSON value. The members of an object keep the order of the text (each key once); the
// elements of an array and the values of the members of an object are its items.
class Value {
public:
    Value() = default; // null
    [[nodiscard]] static Value boolean(bool b);
    [[nodiscard]] static Value number(double v);
    [[nodiscard]] static Value string(std::string s);
    [[nodiscard]] static Value array();
    [[nodiscard]] static Value object();

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool isNull() const { return type_ == Type::Null; }
    [[nodiscard]] bool isBoolean() const { return type_ == Type::Boolean; }
    [[nodiscard]] bool isNumber() const { return type_ == Type::Number; }
    [[nodiscard]] bool isString() const { return type_ == Type::String; }
    [[nodiscard]] bool isArray() const { return type_ == Type::Array; }
    [[nodiscard]] bool isObject() const { return type_ == Type::Object; }

    // The value of a boolean, a number, a string (any other type: false, 0, empty).
    [[nodiscard]] bool asBoolean() const { return boolean_; }
    [[nodiscard]] double asNumber() const { return number_; }
    [[nodiscard]] const std::string& asString() const { return string_; }

    // The elements of an array, or the values of the members of an object.
    [[nodiscard]] const std::vector<Value>& items() const { return items_; }
    [[nodiscard]] std::size_t size() const { return items_.size(); }
    // The keys of the members of an object, in order (items()[k] is the value of keys()[k]).
    [[nodiscard]] const std::vector<std::string>& keys() const { return keys_; }
    // The value of a member of an object, or nullptr.
    [[nodiscard]] const Value* find(std::string_view key) const;

    // Appends an element to an array.
    void push(Value v);
    // Sets a member of an object: replaced in place if the key is there, else appended.
    void set(std::string key, Value v);

    // Where the value starts in the text it was read from (1-based), 0 for a value built.
    [[nodiscard]] int line() const { return line_; }
    [[nodiscard]] int column() const { return column_; }
    void setPosition(int line, int column) {
        line_ = line;
        column_ = column;
    }

    // The same type and contents (numbers by their bits), whatever the positions.
    friend bool operator==(const Value& a, const Value& b);

private:
    Type type_ = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<std::string> keys_;
    std::vector<Value> items_;
    int line_ = 0;
    int column_ = 0;
};

// Nesting deeper than this is refused (the parser is recursive).
inline constexpr int kMaxDepth = 64;

// Parses a whole JSON text (RFC 8259) strictly: a single value, no comment, no trailing comma,
// no duplicate key, no NaN or infinity, no leading zero, valid UTF-8 and escapes. A number must
// be exact as a decimal m 10^e with m at most 2^53 once its trailing zeros are moved into e and
// |e| at most 22 (decimalToDouble): every value a data file needs. Throws Error
// "<source>:<line>:<column>: <reason>".
[[nodiscard]] Value parse(std::string_view text, std::string_view source = "<json>");
// The same on the contents of a file, the source being its path. Throws Error if it cannot be
// read.
[[nodiscard]] Value parseFile(const std::filesystem::path& file);

// The double nearest to m 10^e (negated if negative) when m <= 2^53 and |e| <= 22: m and 10^|e|
// are both exact doubles, so one IEEE multiplication or division rounds it correctly, the same
// bits on every platform (Clinger's fast path). False, out untouched, otherwise.
[[nodiscard]] bool decimalToDouble(std::uint64_t m, int e, bool negative, double& out);

// The canonical text of a value: members in their order, strings escaped (\" \\ and the control
// characters), each number as the shortest decimal that decimalToDouble reads back to the same
// bits. Pretty: 2 spaces per level, one member or element per line; otherwise no space at all.
// Throws Error for a number without such a decimal (NaN, infinities, more than 17 digits).
[[nodiscard]] std::string write(const Value& v, bool pretty = true);

// The 64-bit FNV-1a hash of bytes: the fingerprint of a canonical text.
[[nodiscard]] std::uint64_t fnv1a64(std::string_view bytes);

} // namespace terrain::json
