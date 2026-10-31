#pragma once

// What terrain_probe says of a point, in a fixed order (probe_sections.hpp): fields of numbers
// (kept exact, printed with a fixed number of decimals and a unit), integers, texts, flags,
// absent values, sub-records and lists of records. Written as readable text or as JSON with a
// stable order of its keys; a section gives the same keys at every point, absent values
// included, so that the schema of the JSON never depends on the point.

#include <bit>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace probe {

struct Field;

class Record {
public:
    // Each returns this record, to chain the fields.
    Record& number(std::string key, double value, int decimals, std::string unit = {});
    Record& integer(std::string key, std::int64_t value);
    Record& text(std::string key, std::string value);
    Record& flag(std::string key, bool value);
    Record& none(std::string key);
    Record& add(std::string key, Record child);
    Record& list(std::string key, std::vector<Record> items);
    Record& put(Field field); // a field of another record

    [[nodiscard]] const std::vector<Field>& fields() const { return fields_; }
    // The field of this key, or null.
    [[nodiscard]] const Field* find(const std::string& key) const;

    friend bool operator==(const Record&, const Record&);

private:
    std::vector<Field> fields_;
};

struct Number {
    double value = 0.0;
    int decimals = 0;
    std::string unit;

    friend bool operator==(const Number& a, const Number& b) {
        // Bit for bit: the probe gives the core's values, never values close to them.
        return a.decimals == b.decimals && a.unit == b.unit &&
               std::bit_cast<std::uint64_t>(a.value) == std::bit_cast<std::uint64_t>(b.value);
    }
};

struct Absent {
    friend bool operator==(const Absent&, const Absent&) = default;
};

struct Field {
    std::string key;
    std::variant<Number, std::int64_t, std::string, bool, Absent, Record, std::vector<Record>>
        value;

    friend bool operator==(const Field&, const Field&) = default;
};

// The record as JSON, indented by two spaces, one field per line, ending with a newline.
[[nodiscard]] std::string toJson(const Record& record);
// The record as indented text: "key: value unit", a sub-record under its key, the items of a
// list numbered.
[[nodiscard]] std::string toText(const Record& record, int indent = 0);
// The key paths of the record ("relief.ground", "falls.near[].top"), once each, in order: the
// schema of its JSON. A list of records gives the union of the paths of its items.
[[nodiscard]] std::vector<std::string> schemaOf(const Record& record);
// The scalar fields of a record, flattened ("relief.ground"), with their values as printed; a
// list gives its count ("falls.near" -> "2"). The columns of a table along a line.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> flatten(const Record& record);

} // namespace probe
