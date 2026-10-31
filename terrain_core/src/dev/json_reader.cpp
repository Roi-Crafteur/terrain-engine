#include "terrain/dev/json_value.hpp"

#include "json_number.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

// The strict reader of parseJson (json_value.hpp).
namespace terrain::dev {

namespace {

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// The reader: a cursor over the text, which knows its line and column for the errors.
class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {
        if (text_.substr(0, 3) == "\xEF\xBB\xBF") {
            pos_ = 3;
            lineStart_ = 3;
        }
    }

    JsonValue document() {
        skipSpace();
        JsonValue value = valueAt(0);
        skipSpace();
        if (pos_ < text_.size()) {
            fail("unexpected text after the value");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError(line_, pos_ - lineStart_ + 1, message);
    }

    [[nodiscard]] bool atEnd() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipSpace() {
        while (!atEnd()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++line_;
                lineStart_ = pos_ + 1;
            } else if (c != ' ' && c != '\t' && c != '\r') {
                return;
            }
            ++pos_;
        }
    }

    void expect(char c, const char* what) {
        if (peek() != c) {
            fail(std::string("expected ") + what);
        }
        ++pos_;
    }

    void literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            fail("expected a value (an object, an array, a string, a number, true, false or "
                 "null)");
        }
        pos_ += word.size();
    }

    JsonValue valueAt(int depth) {
        if (depth >= kJsonMaxDepth) {
            fail("values nested too deep");
        }
        const char c = peek();
        JsonValue value;
        if (c == '{') {
            value = objectAt(depth);
        } else if (c == '[') {
            value = arrayAt(depth);
        } else if (c == '"') {
            value = JsonValue::makeString(stringAt());
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            value = JsonValue::makeNumber(numberAt());
        } else if (c == 't') {
            literal("true");
            value = JsonValue::makeBool(true);
        } else if (c == 'f') {
            literal("false");
            value = JsonValue::makeBool(false);
        } else {
            literal("null");
        }
        return value;
    }

    JsonValue objectAt(int depth) {
        ++pos_; // {
        JsonValue object = JsonValue::makeObject();
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            return object;
        }
        while (true) {
            skipSpace();
            if (peek() != '"') {
                fail("expected a key (a string)");
            }
            std::string key = stringAt();
            if (object.find(key) != nullptr) {
                fail("duplicate key " + quoteJson(key));
            }
            skipSpace();
            expect(':', "':' after the key");
            skipSpace();
            object.members.push_back({std::move(key), valueAt(depth + 1)});
            skipSpace();
            if (peek() == '}') {
                ++pos_;
                return object;
            }
            expect(',', "',' or '}'");
        }
    }

    JsonValue arrayAt(int depth) {
        ++pos_; // [
        JsonValue array = JsonValue::makeArray();
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            return array;
        }
        while (true) {
            skipSpace();
            array.items.push_back(valueAt(depth + 1));
            skipSpace();
            if (peek() == ']') {
                ++pos_;
                return array;
            }
            expect(',', "',' or ']'");
        }
    }

    std::size_t digits() {
        const std::size_t start = pos_;
        while (peek() >= '0' && peek() <= '9') {
            ++pos_;
        }
        return pos_ - start;
    }

    double numberAt() {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        if (peek() == '0') {
            ++pos_;
            if (peek() >= '0' && peek() <= '9') {
                fail("a number never starts with 0 followed by a digit");
            }
        } else if (digits() == 0) {
            fail("expected a digit");
        }
        if (peek() == '.') {
            ++pos_;
            if (digits() == 0) {
                fail("expected a digit after the decimal point");
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (digits() == 0) {
                fail("expected a digit in the exponent");
            }
        }
        const double value = readJsonNumber(std::string(text_.substr(start, pos_ - start)));
        if (!std::isfinite(value)) {
            pos_ = start;
            fail("number out of the range of a double");
        }
        return value;
    }

    std::uint32_t hexAt() {
        std::uint32_t code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = peek();
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("expected 4 hexadecimal digits after \\u");
            }
            code = code * 16 + digit;
            ++pos_;
        }
        return code;
    }

    // \uXXXX, with its low surrogate after a high one.
    std::uint32_t unicodeEscape() {
        const std::uint32_t code = hexAt();
        if (code >= 0xDC00 && code <= 0xDFFF) {
            fail("a low surrogate without its high surrogate");
        }
        if (code < 0xD800 || code > 0xDBFF) {
            return code;
        }
        if (text_.substr(pos_, 2) != "\\u") {
            fail("a high surrogate without its low surrogate");
        }
        pos_ += 2;
        const std::uint32_t low = hexAt();
        if (low < 0xDC00 || low > 0xDFFF) {
            fail("a high surrogate without its low surrogate");
        }
        return 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
    }

    void escapeAt(std::string& out) {
        ++pos_; // backslash
        const char c = peek();
        ++pos_;
        switch (c) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': appendUtf8(out, unicodeEscape()); break;
        default:
            --pos_;
            fail("unknown escape in a string");
        }
    }

    // One UTF-8 sequence of 2 to 4 bytes, checked (no overlong form, no surrogate).
    void utf8At(std::string& out) {
        const auto lead = static_cast<unsigned char>(text_[pos_]);
        std::size_t length = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
        } else {
            fail("invalid UTF-8 in a string");
        }
        std::uint32_t code = lead & (0xFFu >> (length + 1));
        for (std::size_t i = 1; i < length; ++i) {
            const std::size_t at = pos_ + i;
            const auto next = static_cast<unsigned char>(at < text_.size() ? text_[at] : '\0');
            if ((next & 0xC0) != 0x80) {
                fail("invalid UTF-8 in a string");
            }
            code = (code << 6) | (next & 0x3Fu);
        }
        const std::uint32_t least = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
        if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            fail("invalid UTF-8 in a string");
        }
        out.append(text_.substr(pos_, length));
        pos_ += length;
    }

    std::string stringAt() {
        ++pos_; // "
        std::string out;
        while (true) {
            if (atEnd()) {
                fail("unterminated string");
            }
            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (c < 0x20) {
                fail("control character in a string (escape it)");
            }
            if (c == '\\') {
                escapeAt(out);
            } else if (c < 0x80) {
                out += static_cast<char>(c);
                ++pos_;
            } else {
                utf8At(out);
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t lineStart_ = 0;
};

} // namespace

JsonValue parseJson(std::string_view text) {
    return Reader(text).document();
}

} // namespace terrain::dev
