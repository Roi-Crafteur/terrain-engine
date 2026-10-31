#include "terrain/json/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace terrain::json {

namespace {

constexpr std::uint64_t kMaxExactMantissa = std::uint64_t{1} << 53;
constexpr int kMaxExactExponent = 22;
constexpr int kMaxExponentDigits = 4;

// 10^0 to 10^22: every one an exact double.
constexpr std::array<double, 23> kPowersOfTen = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80u) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800u) {
        out += static_cast<char>(0xC0u | (cp >> 6));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out += static_cast<char>(0xE0u | (cp >> 12));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (cp >> 18));
        out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
}

// The bytes of a well-formed UTF-8 sequence starting with lead (0 if lead cannot start one),
// and the range of its second byte.
struct Utf8Lead {
    int length = 0;
    unsigned char low = 0x80u;
    unsigned char high = 0xBFu;
};

Utf8Lead utf8Lead(unsigned char lead) {
    if (lead >= 0xC2u && lead <= 0xDFu) {
        return {2, 0x80u, 0xBFu};
    }
    if (lead == 0xE0u) {
        return {3, 0xA0u, 0xBFu};
    }
    if ((lead >= 0xE1u && lead <= 0xECu) || lead == 0xEEu || lead == 0xEFu) {
        return {3, 0x80u, 0xBFu};
    }
    if (lead == 0xEDu) {
        return {3, 0x80u, 0x9Fu}; // no surrogates
    }
    if (lead == 0xF0u) {
        return {4, 0x90u, 0xBFu};
    }
    if (lead >= 0xF1u && lead <= 0xF3u) {
        return {4, 0x80u, 0xBFu};
    }
    if (lead == 0xF4u) {
        return {4, 0x80u, 0x8Fu};
    }
    return {};
}

class Parser {
public:
    Parser(std::string_view text, std::string_view source) : text_(text), source_(source) {}

    Value document() {
        skipSpace();
        Value v = value(0);
        skipSpace();
        if (!atEnd()) {
            fail("unexpected text after the value");
        }
        return v;
    }

private:
    [[nodiscard]] bool atEnd() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const { return atEnd() ? '\0' : text_[pos_]; }
    [[nodiscard]] int column() const { return static_cast<int>(pos_ - lineStart_) + 1; }

    void advance() {
        if (text_[pos_] == '\n') {
            ++line_;
            lineStart_ = pos_ + 1;
        }
        ++pos_;
    }

    [[noreturn]] void fail(const std::string& reason) const {
        std::ostringstream message;
        message << source_ << ':' << line_ << ':' << column() << ": " << reason;
        throw Error(message.str());
    }

    void expect(char c, const char* what) {
        if (peek() != c || atEnd()) {
            fail(std::string("expected ") + what);
        }
        advance();
    }

    void skipSpace() {
        while (!atEnd()) {
            const char c = peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                return;
            }
            advance();
        }
    }

    Value value(int depth) {
        if (depth >= kMaxDepth) {
            fail("nesting deeper than 64 levels");
        }
        const int line = line_;
        const int col = column();
        Value v = valueAt(depth);
        v.setPosition(line, col);
        return v;
    }

    Value valueAt(int depth) {
        switch (peek()) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
        case '"':
            return Value::string(parseString());
        case 't':
            literal("true");
            return Value::boolean(true);
        case 'f':
            literal("false");
            return Value::boolean(false);
        case 'n':
            literal("null");
            return Value{};
        default:
            if (peek() == '-' || isDigit(peek())) {
                return parseNumber();
            }
            fail(atEnd() ? "unexpected end of the text" : "expected a value");
        }
    }

    void literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            fail("expected a value");
        }
        pos_ += word.size();
    }

    Value parseObject(int depth) {
        advance(); // {
        Value object = Value::object();
        skipSpace();
        if (peek() == '}') {
            advance();
            return object;
        }
        for (;;) {
            skipSpace();
            if (peek() != '"') {
                fail("expected a key between quotes");
            }
            const int keyLine = line_;
            const int keyColumn = column();
            std::string key = parseString();
            if (object.find(key) != nullptr) {
                line_ = keyLine;
                pos_ = lineStart_ + static_cast<std::size_t>(keyColumn - 1);
                fail("duplicate key \"" + key + "\"");
            }
            skipSpace();
            expect(':', "':' after the key");
            skipSpace();
            object.set(std::move(key), value(depth + 1));
            skipSpace();
            if (peek() == ',') {
                advance();
                continue;
            }
            expect('}', "',' or '}'");
            return object;
        }
    }

    Value parseArray(int depth) {
        advance(); // [
        Value array = Value::array();
        skipSpace();
        if (peek() == ']') {
            advance();
            return array;
        }
        for (;;) {
            skipSpace();
            array.push(value(depth + 1));
            skipSpace();
            if (peek() == ',') {
                advance();
                continue;
            }
            expect(']', "',' or ']'");
            return array;
        }
    }

    std::uint32_t hex4() {
        std::uint32_t cp = 0;
        for (int k = 0; k < 4; ++k) {
            const int d = atEnd() ? -1 : hexDigit(peek());
            if (d < 0) {
                fail("expected 4 hexadecimal digits after \\u");
            }
            cp = cp * 16u + static_cast<std::uint32_t>(d);
            advance();
        }
        return cp;
    }

    void escape(std::string& out) {
        advance(); // backslash
        const char c = peek();
        if (atEnd()) {
            fail("unexpected end of the text in a string");
        }
        constexpr std::string_view kFrom = "\"\\/bfnrt";
        constexpr std::string_view kTo = "\"\\/\b\f\n\r\t";
        if (const std::size_t k = kFrom.find(c); k != std::string_view::npos) {
            out += kTo[k];
            advance();
            return;
        }
        if (c != 'u') {
            fail("invalid escape in a string");
        }
        advance();
        std::uint32_t cp = hex4();
        if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            fail("lone low surrogate in a string");
        }
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            if (text_.substr(pos_, 2) != "\\u") {
                fail("high surrogate without its low surrogate");
            }
            pos_ += 2;
            const std::uint32_t low = hex4();
            if (low < 0xDC00u || low > 0xDFFFu) {
                fail("high surrogate without its low surrogate");
            }
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
        }
        appendUtf8(out, cp);
    }

    void copyUtf8(std::string& out) {
        const auto lead = static_cast<unsigned char>(peek());
        const Utf8Lead seq = utf8Lead(lead);
        if (seq.length == 0) {
            fail("invalid UTF-8 in a string");
        }
        for (int k = 1; k < seq.length; ++k) {
            const std::size_t at = pos_ + static_cast<std::size_t>(k);
            const auto byte = at < text_.size() ? static_cast<unsigned char>(text_[at]) : 0u;
            const unsigned char low = k == 1 ? seq.low : 0x80u;
            const unsigned char high = k == 1 ? seq.high : 0xBFu;
            if (byte < low || byte > high) {
                fail("invalid UTF-8 in a string");
            }
        }
        out.append(text_.substr(pos_, static_cast<std::size_t>(seq.length)));
        pos_ += static_cast<std::size_t>(seq.length);
    }

    std::string parseString() {
        advance(); // opening quote
        std::string out;
        for (;;) {
            if (atEnd()) {
                fail("unexpected end of the text in a string");
            }
            const auto c = static_cast<unsigned char>(peek());
            if (c == '"') {
                advance();
                return out;
            }
            if (c < 0x20u) {
                fail("control character in a string");
            }
            if (c == '\\') {
                escape(out);
            } else if (c >= 0x80u) {
                copyUtf8(out);
            } else {
                out += static_cast<char>(c);
                advance();
            }
        }
    }

    // The digits of a number: m 10^(e + pending), the zeros after the last nonzero digit kept
    // apart so that long runs of them never overflow m.
    struct Decimal {
        std::uint64_t m = 0;
        int e = 0;
        int pending = 0;
    };

    void digit(Decimal& d, int value, bool fraction) {
        if (fraction) {
            --d.e;
        }
        if (value == 0) {
            if (d.m != 0) {
                ++d.pending;
            }
            return;
        }
        for (int k = 0; k <= d.pending; ++k) {
            if (d.m > kMaxExactMantissa / 10u) {
                fail("number with too many significant digits to be exact");
            }
            d.m *= 10u;
        }
        d.pending = 0;
        d.m += static_cast<std::uint64_t>(value);
        if (d.m > kMaxExactMantissa) {
            fail("number with too many significant digits to be exact");
        }
    }

    void digits(Decimal& d, bool fraction) {
        if (atEnd() || !isDigit(peek())) {
            fail("expected a digit");
        }
        while (!atEnd() && isDigit(peek())) {
            digit(d, peek() - '0', fraction);
            advance();
        }
    }

    int exponent() {
        advance(); // e or E
        bool negative = false;
        if (peek() == '+' || peek() == '-') {
            negative = peek() == '-';
            advance();
        }
        if (atEnd() || !isDigit(peek())) {
            fail("expected a digit in the exponent");
        }
        int value = 0;
        int count = 0;
        while (!atEnd() && isDigit(peek())) {
            if (++count > kMaxExponentDigits) {
                fail("exponent too large");
            }
            value = value * 10 + (peek() - '0');
            advance();
        }
        return negative ? -value : value;
    }

    Value parseNumber() {
        const bool negative = peek() == '-';
        if (negative) {
            advance();
        }
        if (peek() == '0' && pos_ + 1 < text_.size() && isDigit(text_[pos_ + 1])) {
            fail("leading zero in a number");
        }
        Decimal d;
        digits(d, false);
        if (peek() == '.') {
            advance();
            digits(d, true);
        }
        if (peek() == 'e' || peek() == 'E') {
            d.e += exponent();
        }
        double v = 0.0;
        if (d.m == 0) {
            return Value::number(negative ? -0.0 : 0.0);
        }
        if (!decimalToDouble(d.m, d.e + d.pending, negative, v)) {
            fail("number not exactly representable (at most 2^53 times 10^+-22)");
        }
        return Value::number(v);
    }

    std::string_view text_;
    std::string_view source_;
    std::size_t pos_ = 0;
    std::size_t lineStart_ = 0;
    int line_ = 1;
};

} // namespace

bool decimalToDouble(std::uint64_t m, int e, bool negative, double& out) {
    if (m > kMaxExactMantissa) {
        return false;
    }
    // Large exponents: move what fits into the mantissa, which stays exact.
    while (e > kMaxExactExponent && m != 0 && m <= kMaxExactMantissa / 10u) {
        m *= 10u;
        --e;
    }
    if (e > kMaxExactExponent || e < -kMaxExactExponent) {
        if (m != 0) {
            return false;
        }
        e = 0;
    }
    const auto mantissa = static_cast<double>(m);
    const double v = e >= 0 ? mantissa * kPowersOfTen[static_cast<std::size_t>(e)]
                            : mantissa / kPowersOfTen[static_cast<std::size_t>(-e)];
    out = negative ? -v : v;
    return true;
}

Value parse(std::string_view text, std::string_view source) {
    return Parser(text, source).document();
}

Value parseFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw Error(file.generic_string() + ": cannot be read");
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        throw Error(file.generic_string() + ": cannot be read");
    }
    return parse(text, file.generic_string());
}

} // namespace terrain::json
