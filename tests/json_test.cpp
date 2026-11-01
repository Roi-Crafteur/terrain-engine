// The JSON of the data files (terrain/json/json.hpp): strict reading, numbers to the bit on every
// platform, clear errors with their line and column, canonical writing.

#include "terrain/json/json.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace json = terrain::json;

namespace {

std::uint64_t bits(double v) {
    return std::bit_cast<std::uint64_t>(v);
}

bool sameNumber(std::string_view text, double expected) {
    return bits(json::parse(text).asNumber()) == bits(expected);
}

// The message of the error of text, or "" if it parses.
std::string errorOf(std::string_view text) {
    try {
        static_cast<void>(json::parse(text, "test.json"));
    } catch (const json::Error& e) {
        return e.what();
    }
    return "";
}

bool fails(std::string_view text, std::string_view where, std::string_view reason) {
    const std::string message = errorOf(text);
    const bool ok = message.find(where) != std::string::npos &&
                    message.find(reason) != std::string::npos;
    if (!ok) {
        std::printf("unexpected error for %.*s: '%s'\n", static_cast<int>(text.size()),
                    text.data(), message.c_str());
    }
    return ok;
}

void testValues() {
    const json::Value v = json::parse(
        "{ \"b\": [1, 2.5, \"x\", true, false, null],\n  \"a\": {\"inner\": {}} }");
    TEST_CHECK(v.isObject());
    TEST_CHECK(v.keys().size() == 2 && v.keys()[0] == "b" && v.keys()[1] == "a");
    const json::Value* b = v.find("b");
    TEST_CHECK(b != nullptr && b->isArray() && b->size() == 6);
    TEST_CHECK(b->items()[0].asNumber() == 1.0 && b->items()[1].asNumber() == 2.5);
    TEST_CHECK(b->items()[2].asString() == "x");
    TEST_CHECK(b->items()[3].asBoolean() && !b->items()[4].asBoolean());
    TEST_CHECK(b->items()[5].isNull());
    TEST_CHECK(v.find("a")->find("inner")->isObject());
    TEST_CHECK(v.find("missing") == nullptr);
    // Positions: "a" starts on line 2.
    TEST_CHECK(v.find("a")->line() == 2 && v.find("a")->column() == 8);
}

void testNumbers() {
    TEST_CHECK(sameNumber("0.05", 0.05));
    TEST_CHECK(sameNumber("0.1", 0.1));
    TEST_CHECK(sameNumber("1500", 1500.0));
    TEST_CHECK(sameNumber("-30", -30.0));
    TEST_CHECK(sameNumber("1e-3", 0.001));
    TEST_CHECK(sameNumber("2.5E3", 2500.0));
    TEST_CHECK(sameNumber("0.525", 0.525));
    TEST_CHECK(sameNumber("2101000000", 2101000000.0));
    TEST_CHECK(sameNumber("123456789012345", 123456789012345.0));
    TEST_CHECK(sameNumber("9007199254740992", 9007199254740992.0));
    TEST_CHECK(sameNumber("1e22", 1e22));
    TEST_CHECK(sameNumber("1e30", 1e30));
    TEST_CHECK(sameNumber("100000000000000000000000000", 1e26));
    TEST_CHECK(sameNumber("0.000001", 1e-6));
    TEST_CHECK(sameNumber("1.000", 1.0));
    TEST_CHECK(bits(json::parse("-0").asNumber()) == bits(-0.0));
    TEST_CHECK(fails("9007199254740993", ":1:", "significant digits"));
    TEST_CHECK(fails("12345678901234567890", ":1:", "significant digits"));
    TEST_CHECK(fails("1e-30", ":1:", "not exactly representable"));
    TEST_CHECK(fails("1e99999", ":1:", "exponent too large"));
}

void testErrors() {
    TEST_CHECK(fails("[1,]", "test.json:1:4:", "expected a value"));
    TEST_CHECK(fails("{\"a\": 1,}", "test.json:1:9:", "expected a key"));
    TEST_CHECK(fails("{\"a\": 1,\n \"a\": 2}", "test.json:2:2:", "duplicate key \"a\""));
    TEST_CHECK(fails("\"\\x\"", ":1:3:", "invalid escape"));
    TEST_CHECK(fails("NaN", ":1:1:", "expected a value"));
    TEST_CHECK(fails("[Infinity]", ":1:2:", "expected a value"));
    TEST_CHECK(fails("01", ":1:1:", "leading zero"));
    TEST_CHECK(fails("-", ":1:2:", "expected a digit"));
    TEST_CHECK(fails("1.", ":1:3:", "expected a digit"));
    TEST_CHECK(fails("\"a\tb\"", ":1:3:", "control character"));
    TEST_CHECK(fails("\"\xC3\x28\"", ":1:2:", "invalid UTF-8"));
    TEST_CHECK(fails("\"\xED\xA0\x80\"", ":1:2:", "invalid UTF-8"));
    TEST_CHECK(fails("\"\\udc00\"", ":1:", "lone low surrogate"));
    TEST_CHECK(fails("\"\\ud800x\"", ":1:", "without its low surrogate"));
    TEST_CHECK(fails("{\"a\" 1}", ":1:6:", "expected ':'"));
    TEST_CHECK(fails("[1 2]", ":1:4:", "expected ',' or ']'"));
    TEST_CHECK(fails("[1", ":1:3:", "expected ',' or ']'"));
    TEST_CHECK(fails("\"abc", ":1:5:", "end of the text in a string"));
    TEST_CHECK(fails("", ":1:1:", "unexpected end"));
    TEST_CHECK(fails("1 2", ":1:3:", "after the value"));
    TEST_CHECK(fails("// comment\n1", ":1:1:", "expected a value"));
    TEST_CHECK(fails(std::string(65, '[') + std::string(65, ']'), ":1:65:", "deeper than 64"));
    TEST_CHECK(errorOf(std::string(64, '[') + std::string(64, ']')).empty());
}

void testStrings() {
    TEST_CHECK(json::parse("\"\\u00e9t\\u00e9\"").asString() == "\xC3\xA9t\xC3\xA9");
    TEST_CHECK(json::parse("\"\\ud83d\\ude00\"").asString() == "\xF0\x9F\x98\x80");
    TEST_CHECK(json::parse("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"").asString() == "\"\\/\b\f\n\r\t");
    TEST_CHECK(json::parse("\"Géants\"").asString() == "Géants");
}

void testWrite() {
    const std::string text = "{\"a\":[1,0.05,-30,true,null,1e30,0.001],\"b\":\"x\\\"y\\n\"}";
    const json::Value v = json::parse(text);
    TEST_CHECK(json::write(v, false) == text);
    TEST_CHECK(json::write(json::parse("{\"k\": [1, {}], \"e\": []}"), true) ==
               "{\n  \"k\": [\n    1,\n    {}\n  ],\n  \"e\": []\n}\n");
    TEST_CHECK(json::parse(json::write(v, true)) == v);
    TEST_CHECK(json::write(json::Value::number(1.0 / 3.0), false) == "0.3333333333333333");
    TEST_CHECK(json::write(json::Value::number(-0.0), false) == "-0");
    TEST_CHECK(json::write(json::Value::string(std::string("\x01", 1)), false) == "\"\\u0001\"");
    bool refused = false;
    try {
        static_cast<void>(json::write(json::Value::number(0.1 + 0.2), false));
    } catch (const json::Error&) {
        refused = true;
    }
    TEST_CHECK(refused);
    // Built values: set replaces in place, push appends.
    json::Value o = json::Value::object();
    o.set("x", json::Value::number(1.0));
    o.set("y", json::Value::array());
    o.set("x", json::Value::number(2.0));
    TEST_CHECK(json::write(o, false) == "{\"x\":2,\"y\":[]}");
    TEST_CHECK(!(json::parse("1") == json::parse("2")));
    TEST_CHECK(json::parse("{\"a\":1}") == json::parse("{ \"a\" : 1 }"));
}

void testHashAndFiles() {
    TEST_CHECK(json::fnv1a64("") == 0xCBF29CE484222325ULL);
    TEST_CHECK(json::fnv1a64("a") == 0xAF63DC4C8601EC8CULL);
    bool unreadable = false;
    try {
        static_cast<void>(json::parseFile("no/such/file.json"));
    } catch (const json::Error& e) {
        unreadable = std::string(e.what()).find("no/such/file.json") != std::string::npos;
    }
    TEST_CHECK(unreadable);
}

} // namespace

int main() {
    testValues();
    testNumbers();
    testErrors();
    testStrings();
    testWrite();
    testHashAndFiles();
    return test::result();
}
