// The records of terrain_probe and their text and JSON (probe_record.hpp).
#include "probe_record.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace probe {

Record& Record::number(std::string key, double value, int decimals, std::string unit) {
    fields_.push_back({std::move(key), Number{value, decimals, std::move(unit)}});
    return *this;
}

Record& Record::integer(std::string key, std::int64_t value) {
    fields_.push_back({std::move(key), value});
    return *this;
}

Record& Record::text(std::string key, std::string value) {
    fields_.push_back({std::move(key), std::move(value)});
    return *this;
}

Record& Record::flag(std::string key, bool value) {
    fields_.push_back({std::move(key), value});
    return *this;
}

Record& Record::none(std::string key) {
    fields_.push_back({std::move(key), Absent{}});
    return *this;
}

Record& Record::add(std::string key, Record child) {
    fields_.push_back({std::move(key), std::move(child)});
    return *this;
}

Record& Record::list(std::string key, std::vector<Record> items) {
    fields_.push_back({std::move(key), std::move(items)});
    return *this;
}

Record& Record::put(Field field) {
    fields_.push_back(std::move(field));
    return *this;
}

const Field* Record::find(const std::string& key) const {
    const auto it = std::find_if(fields_.begin(), fields_.end(),
                                 [&](const Field& f) { return f.key == key; });
    return it == fields_.end() ? nullptr : &*it;
}

bool operator==(const Record& a, const Record& b) { return a.fields_ == b.fields_; }

namespace {

// A number with its decimals; "-0.00" printed as "0.00". Empty for a value that is not finite.
std::string printed(const Number& n) {
    if (!std::isfinite(n.value)) {
        return {};
    }
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", n.decimals, n.value);
    std::string s = text;
    if (s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) {
        s.erase(0, 1);
    }
    return s;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char escape[8];
            std::snprintf(escape, sizeof escape, "\\u%04x", static_cast<unsigned>(c));
            out += escape;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// The scalar value of a field as text, or empty for a record or a list (absent: "-").
std::string scalarText(const Field& f, bool json) {
    return std::visit(
        [json](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Number>) {
                const std::string s = printed(v);
                return s.empty() ? (json ? "null" : "-") : s;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return json ? quoted(v) : v;
            } else if constexpr (std::is_same_v<T, bool>) {
                return json ? (v ? "true" : "false") : (v ? "yes" : "no");
            } else if constexpr (std::is_same_v<T, Absent>) {
                return json ? "null" : "-";
            } else {
                return {};
            }
        },
        f.value);
}

void appendJson(const Record& record, int indent, std::string& out) {
    if (record.fields().empty()) {
        out += "{}";
        return;
    }
    const std::string pad(static_cast<std::size_t>(indent + 2), ' ');
    out += "{\n";
    for (std::size_t i = 0; i < record.fields().size(); ++i) {
        const Field& f = record.fields()[i];
        out += pad + quoted(f.key) + ": ";
        if (const auto* child = std::get_if<Record>(&f.value)) {
            appendJson(*child, indent + 2, out);
        } else if (const auto* items = std::get_if<std::vector<Record>>(&f.value)) {
            if (items->empty()) {
                out += "[]";
            } else {
                out += "[\n";
                for (std::size_t k = 0; k < items->size(); ++k) {
                    out += pad + "  ";
                    appendJson((*items)[k], indent + 4, out);
                    out += k + 1 < items->size() ? ",\n" : "\n";
                }
                out += pad + "]";
            }
        } else {
            out += scalarText(f, true);
        }
        out += i + 1 < record.fields().size() ? ",\n" : "\n";
    }
    out += std::string(static_cast<std::size_t>(indent), ' ') + "}";
}

void addPath(std::vector<std::string>& paths, const std::string& path) {
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
    }
}

void appendSchema(const Record& record, const std::string& prefix,
                  std::vector<std::string>& paths) {
    for (const Field& f : record.fields()) {
        const std::string path = prefix.empty() ? f.key : prefix + "." + f.key;
        addPath(paths, path);
        if (const auto* child = std::get_if<Record>(&f.value)) {
            appendSchema(*child, path, paths);
        } else if (const auto* items = std::get_if<std::vector<Record>>(&f.value)) {
            for (const Record& item : *items) {
                appendSchema(item, path + "[]", paths);
            }
        }
    }
}

void appendFlat(const Record& record, const std::string& prefix,
                std::vector<std::pair<std::string, std::string>>& out) {
    for (const Field& f : record.fields()) {
        const std::string path = prefix.empty() ? f.key : prefix + "." + f.key;
        if (const auto* child = std::get_if<Record>(&f.value)) {
            appendFlat(*child, path, out);
        } else if (const auto* items = std::get_if<std::vector<Record>>(&f.value)) {
            out.emplace_back(path, std::to_string(items->size()));
        } else {
            out.emplace_back(path, scalarText(f, false));
        }
    }
}

} // namespace

std::string toJson(const Record& record) {
    std::string out;
    appendJson(record, 0, out);
    return out + "\n";
}

std::string toText(const Record& record, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    std::string out;
    for (const Field& f : record.fields()) {
        if (const auto* child = std::get_if<Record>(&f.value)) {
            out += pad + f.key + ":\n" + toText(*child, indent + 2);
        } else if (const auto* items = std::get_if<std::vector<Record>>(&f.value)) {
            out += pad + f.key + ": " + std::to_string(items->size()) + "\n";
            for (std::size_t k = 0; k < items->size(); ++k) {
                out += pad + "  #" + std::to_string(k + 1) + "\n" +
                       toText((*items)[k], indent + 4);
            }
        } else {
            const auto* n = std::get_if<Number>(&f.value);
            const bool unit = n != nullptr && !n->unit.empty() && std::isfinite(n->value);
            out += pad + f.key + ": " + scalarText(f, false) + (unit ? " " + n->unit : "") + "\n";
        }
    }
    return out;
}

std::vector<std::string> schemaOf(const Record& record) {
    std::vector<std::string> paths;
    appendSchema(record, "", paths);
    return paths;
}

std::vector<std::pair<std::string, std::string>> flatten(const Record& record) {
    std::vector<std::pair<std::string, std::string>> out;
    appendFlat(record, "", out);
    return out;
}

} // namespace probe
