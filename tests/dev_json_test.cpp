// The strict JSON of the files of the developers (terrain_core dev/json_value.hpp): what it reads,
// what it refuses and where, and numbers that read back to the same bits.
#include "test_assert.hpp"

#include "terrain/dev/json_value.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using terrain::dev::formatJsonNumber;
using terrain::dev::JsonError;
using terrain::dev::JsonKind;
using terrain::dev::JsonValue;
using terrain::dev::parseJson;
using terrain::dev::quoteJson;
using terrain::dev::writeJson;

// The message of the error of the text, or empty if it was read.
std::string errorOf(std::string_view text) {
    try {
        (void)parseJson(text);
    } catch (const JsonError& error) {
        return error.what();
    }
    return {};
}

bool refused(std::string_view text, std::string_view part) {
    const std::string message = errorOf(text);
    if (message.find(part) == std::string::npos) {
        std::fprintf(stderr, "  %.*s -> \"%s\", expected \"%.*s\"\n", static_cast<int>(text.size()),
                     text.data(), message.c_str(), static_cast<int>(part.size()), part.data());
        return false;
    }
    return true;
}

void testValues() {
    const JsonValue v = parseJson(
        " {\"a\": [1, -2.5, 3e2, true, false, null], \"b\": {\"c\": \"d\"}, \"e\": []}\n");
    TEST_CHECK(v.kind == JsonKind::Object);
    TEST_CHECK(v.members.size() == 3);
    TEST_CHECK(v.members[0].key == "a" && v.members[1].key == "b" && v.members[2].key == "e");
    const JsonValue* a = v.find("a");
    TEST_CHECK(a != nullptr && a->kind == JsonKind::Array && a->items.size() == 6);
    TEST_CHECK(a->items[0].number == 1.0 && a->items[1].number == -2.5);
    TEST_CHECK(a->items[2].number == 300.0);
    TEST_CHECK(a->items[3].kind == JsonKind::Bool && a->items[3].boolean);
    TEST_CHECK(a->items[4].kind == JsonKind::Bool && !a->items[4].boolean);
    TEST_CHECK(a->items[5].kind == JsonKind::Null);
    TEST_CHECK(v.find("b")->find("c")->text == "d");
    TEST_CHECK(v.find("e")->kind == JsonKind::Array && v.find("e")->items.empty());
    TEST_CHECK(v.find("z") == nullptr && a->find("a") == nullptr);
    TEST_CHECK(writeJson(v) ==
               "{\"a\": [1, -2.5, 300, true, false, null], \"b\": {\"c\": \"d\"}, \"e\": []}");
    TEST_CHECK(parseJson(writeJson(v)) == v);
    // A byte order mark is skipped, scalars stand alone.
    TEST_CHECK(parseJson("\xEF\xBB\xBF{}").kind == JsonKind::Object);
    TEST_CHECK(parseJson("42").number == 42.0);
    TEST_CHECK(parseJson("-0").number == 0.0 && std::signbit(parseJson("-0").number));
}

void testStrings() {
    const JsonValue s = parseJson(R"("q\" b\\ s\/ \b\f\n\r\t \u00e9 \u20AC \ud83d\ude00")");
    TEST_CHECK(s.text == "q\" b\\ s/ \b\f\n\r\t \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80");
    // UTF-8 is kept as it is; quotes, backslashes and control characters are escaped.
    TEST_CHECK(parseJson("\"chute de 388 m vue depuis la l\xC3\xA8vre\"").text ==
               "chute de 388 m vue depuis la l\xC3\xA8vre");
    TEST_CHECK(quoteJson("a\"b\\c\nd\te\x01") == "\"a\\\"b\\\\c\\nd\\te\\u0001\"");
    TEST_CHECK(parseJson(quoteJson(s.text)).text == s.text);
    std::string everyByte;
    for (int c = 1; c < 128; ++c) {
        everyByte += static_cast<char>(c);
    }
    TEST_CHECK(parseJson(quoteJson(everyByte)).text == everyByte);
}

void testRefusals() {
    TEST_CHECK(refused("", "line 1, column 1: expected a value"));
    TEST_CHECK(refused("{\"a\": 1,}", "line 1, column 9: expected a key"));
    TEST_CHECK(refused("[1, 2,]", "line 1, column 7: expected a value"));
    TEST_CHECK(refused("{\n  \"a\": 1\n  \"b\": 2\n}", "line 3, column 3: expected ',' or '}'"));
    TEST_CHECK(refused("[1 2]", "expected ',' or ']'"));
    TEST_CHECK(refused("{\"a\" 1}", "expected ':' after the key"));
    TEST_CHECK(refused("{\"a\": 1, \"a\": 2}", "duplicate key \"a\""));
    TEST_CHECK(refused("{a: 1}", "expected a key"));
    TEST_CHECK(refused("// comment\n{}", "expected a value"));
    TEST_CHECK(refused("{} {}", "line 1, column 4: unexpected text after the value"));
    TEST_CHECK(refused("NaN", "expected a value"));
    TEST_CHECK(refused("[Infinity]", "expected a value"));
    TEST_CHECK(refused("[-]", "expected a digit"));
    TEST_CHECK(refused("[01]", "never starts with 0"));
    TEST_CHECK(refused("[1.]", "after the decimal point"));
    TEST_CHECK(refused("[.5]", "expected a value"));
    TEST_CHECK(refused("[1e]", "in the exponent"));
    TEST_CHECK(refused("[+1]", "expected a value"));
    TEST_CHECK(refused("[1e999]", "out of the range"));
    TEST_CHECK(refused("[tru]", "expected a value"));
    TEST_CHECK(refused("\"abc", "unterminated string"));
    TEST_CHECK(refused("\"a\nb\"", "control character"));
    TEST_CHECK(refused("\"\\x\"", "unknown escape"));
    TEST_CHECK(refused("\"\\u12g4\"", "4 hexadecimal digits"));
    TEST_CHECK(refused("\"\\ud83d\"", "without its low surrogate"));
    TEST_CHECK(refused("\"\\ude00\"", "without its high surrogate"));
    TEST_CHECK(refused("\"\xC3\"", "invalid UTF-8"));
    TEST_CHECK(refused("\"\xC0\xAF\"", "invalid UTF-8"));       // overlong
    TEST_CHECK(refused("\"\xED\xA0\x80\"", "invalid UTF-8"));   // a surrogate
    TEST_CHECK(refused("\"\xF5\x80\x80\x80\"", "invalid UTF-8")); // beyond U+10FFFF
    std::string deep(terrain::dev::kJsonMaxDepth + 1, '[');
    deep += std::string(terrain::dev::kJsonMaxDepth + 1, ']');
    TEST_CHECK(refused(deep, "nested too deep"));
    std::string fine(terrain::dev::kJsonMaxDepth, '[');
    fine += std::string(terrain::dev::kJsonMaxDepth, ']');
    TEST_CHECK(errorOf(fine).empty());
    // The line and column of the error.
    try {
        (void)parseJson("{\n\n   ]");
        TEST_CHECK(false);
    } catch (const JsonError& error) {
        TEST_CHECK(error.line() == 3 && error.column() == 4);
    }
}

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

void testNumbers() {
    TEST_CHECK(formatJsonNumber(182.03) == "182.03");
    TEST_CHECK(formatJsonNumber(-6185104.0) == "-6185104");
    TEST_CHECK(formatJsonNumber(227.0) == "227");
    TEST_CHECK(formatJsonNumber(0.1) == "0.1");
    TEST_CHECK(formatJsonNumber(0.0) == "0" && formatJsonNumber(-0.0) == "-0");
    TEST_CHECK(formatJsonNumber(1e-7) == "1e-07");
    TEST_CHECK(formatJsonNumber(1e21) == "1e+21");
    bool threw = false;
    try {
        (void)formatJsonNumber(std::numeric_limits<double>::quiet_NaN());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
    // Any finite double reads back to its bits: chosen values, then bit patterns all over.
    const std::vector<double> chosen = {0.1 + 0.2, 1.0 / 3.0, -26927500.25, 1725.26, 3.28,
                                        std::numeric_limits<double>::max(),
                                        std::numeric_limits<double>::min(),
                                        std::numeric_limits<double>::denorm_min(),
                                        5e-324, 123456789012345678.0, -1e-300};
    for (const double x : chosen) {
        TEST_CHECK(sameBits(parseJson(formatJsonNumber(x)).number, x));
    }
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    int failures = 0;
    for (int i = 0; i < 20000; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const double x = std::bit_cast<double>(state);
        if (std::isfinite(x) && !sameBits(parseJson(formatJsonNumber(x)).number, x)) {
            ++failures;
        }
    }
    TEST_CHECK(failures == 0);
    // Exponents and fractions are read exactly as strtod rounds them.
    TEST_CHECK(parseJson("1E2").number == 100.0 && parseJson("1e+2").number == 100.0);
    TEST_CHECK(parseJson("2.5e-3").number == 0.0025);
}

void testConsoleNumbers() {
    using terrain::dev::parseJsonNumber;
    TEST_CHECK(parseJsonNumber("182.03") == 182.03);
    TEST_CHECK(parseJsonNumber("-6185104") == -6185104.0);
    TEST_CHECK(parseJsonNumber("1e3") == 1000.0);
    for (const char* bad : {"", " 1", "1 ", "+1", "1.", ".5", "1,5", "abc", "1e", "--1", "0x10",
                            "nan", "inf", "\xEF\xBB\xBF" "1", "1e999", "[1]", "\"1\""}) {
        TEST_CHECK(!parseJsonNumber(bad).has_value());
    }
}

} // namespace

int main() {
    testValues();
    testStrings();
    testRefusals();
    testNumbers();
    testConsoleNumbers();
    return test::result();
}
