#pragma once

// A strict JSON reader for the tests of terrain_probe (probe_test.cpp): true when the text is one
// well-formed JSON value (objects, arrays, strings with their escapes, numbers, true, false, null)
// and nothing after it but white space.

#include <cctype>
#include <cstddef>
#include <string>

namespace probe_test {

class JsonCheck {
public:
    explicit JsonCheck(const std::string& text) : s_(text) {}

    [[nodiscard]] bool wellFormed() {
        i_ = 0;
        return value() && (space(), i_ == s_.size());
    }

private:
    void space() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])) != 0) {
            ++i_;
        }
    }
    bool eat(char c) {
        space();
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }
    bool word(const char* w) {
        const std::string text = w;
        if (s_.compare(i_, text.size(), text) != 0) {
            return false;
        }
        i_ += text.size();
        return true;
    }
    bool string() {
        if (!eat('"')) {
            return false;
        }
        while (i_ < s_.size() && s_[i_] != '"') {
            if (static_cast<unsigned char>(s_[i_]) < 0x20) {
                return false;
            }
            if (s_[i_] == '\\') {
                ++i_;
                const std::string escapes = "\"\\/bfnrtu";
                if (i_ >= s_.size() || escapes.find(s_[i_]) == std::string::npos) {
                    return false;
                }
            }
            ++i_;
        }
        return i_++ < s_.size();
    }
    bool number() {
        const std::size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') {
            ++i_;
        }
        std::size_t digits = 0;
        while (i_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '.')) {
            ++i_;
            ++digits;
        }
        return digits > 0 && i_ > start;
    }
    bool members(char close, bool keys) {
        if (eat(close)) {
            return true;
        }
        do {
            if (keys && !(string() && eat(':'))) {
                return false;
            }
            if (!value()) {
                return false;
            }
        } while (eat(','));
        return eat(close);
    }
    bool value() {
        space();
        if (i_ >= s_.size()) {
            return false;
        }
        switch (s_[i_]) {
        case '{':
            ++i_;
            return members('}', true);
        case '[':
            ++i_;
            return members(']', false);
        case '"':
            return string();
        case 't':
            return word("true");
        case 'f':
            return word("false");
        case 'n':
            return word("null");
        default:
            return number();
        }
    }

    const std::string& s_;
    std::size_t i_ = 0;
};

} // namespace probe_test
