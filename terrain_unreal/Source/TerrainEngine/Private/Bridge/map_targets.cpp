#include "map_targets.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace terrain::unreal {

namespace {

constexpr std::string_view kMinus = "\xE2\x88\x92";         // U+2212
constexpr std::string_view kNoBreak = "\xC2\xA0";           // U+00A0
constexpr std::string_view kNarrowNoBreak = "\xE2\x80\xAF"; // U+202F
constexpr std::string_view kThin = "\xE2\x80\x89";          // U+2009

// The text with the minus and the spaces of typography made plain.
std::string plain(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const std::string_view rest = text.substr(i);
        if (rest.starts_with(kMinus)) {
            out += '-';
            i += kMinus.size();
        } else if (rest.starts_with(kNoBreak)) {
            out += ' ';
            i += kNoBreak.size();
        } else if (rest.starts_with(kNarrowNoBreak) || rest.starts_with(kThin)) {
            out += ' ';
            i += kNarrowNoBreak.size();
        } else {
            out += text[i] == '\t' ? ' ' : text[i];
            ++i;
        }
    }
    return out;
}

// One number: its spaces taken out, a decimal comma read as a point when allowed.
std::optional<double> number(std::string part, bool decimalComma) {
    part.erase(std::remove(part.begin(), part.end(), ' '), part.end());
    if (decimalComma) {
        std::replace(part.begin(), part.end(), ',', '.');
    }
    if (!part.empty() && part.front() == '+') {
        part.erase(part.begin());
    }
    double value = 0.0;
    const char* end = part.data() + part.size();
    const auto [last, error] = std::from_chars(part.data(), end, value);
    if (part.empty() || error != std::errc{} || last != end || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string> splitOn(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = text.find(separator, start);
        parts.push_back(text.substr(start, at - start));
        if (at == std::string::npos) {
            return parts;
        }
        start = at + 1;
    }
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : text + ' ') {
        if (c == ' ') {
            if (!word.empty()) {
                out.push_back(std::exchange(word, {}));
            }
        } else {
            word += c;
        }
    }
    return out;
}

std::string grouped(std::uint64_t whole) {
    std::string digits = std::to_string(whole);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += kNarrowNoBreak;
        }
        out += digits[i];
    }
    return out;
}

} // namespace

dev::Place clickTarget(const MapView& view, ScreenPoint pixel, double heading, double pitch,
                       const WorldSize& world) {
    const WorldPoint p = screenToWorld(view, pixel, world);
    dev::Place place;
    place.name = "map_click";
    place.x = p.x;
    place.z = p.z;
    place.heading = headingOfYaw(yawOfHeading(heading));
    place.pitch = std::clamp(pitch, -90.0, 90.0);
    place.eye = kMapEyeAboveGround;
    place.eyeMode = dev::EyeMode::Ground;
    return place;
}

std::optional<WorldPoint> parseCoordinates(std::string_view text) {
    const std::string t = plain(text);
    std::vector<std::string> parts;
    bool decimalComma = false;
    if (t.find(';') != std::string::npos) {
        parts = splitOn(t, ';');
        decimalComma = true;
    } else if (t.find(',') != std::string::npos) {
        parts = splitOn(t, ',');
    } else {
        parts = words(t);
    }
    if (parts.size() != 2) {
        return std::nullopt;
    }
    const std::optional<double> x = number(parts[0], decimalComma);
    const std::optional<double> z = number(parts[1], decimalComma);
    if (!x || !z) {
        return std::nullopt;
    }
    return WorldPoint{*x, *z};
}

MapGoCommand parseMapGo(const std::vector<std::string>& args) {
    MapGoCommand command;
    std::string joined;
    for (const std::string& arg : args) {
        joined += (joined.empty() ? "" : " ") + arg;
    }
    if (const std::optional<WorldPoint> point = parseCoordinates(joined)) {
        command.point = point;
        return command;
    }
    if (args.size() == 1 && dev::validPlaceName(args[0]) && !number(plain(args[0]), false)) {
        command.place = args[0];
        return command;
    }
    command.error = joined.empty() ? std::string("a point or a place expected")
                                   : "not a point nor a place: \"" + joined + "\"";
    command.error += std::string(" (usage: ") + kMapGoUsage + ")";
    return command;
}

std::string formatMeters(double meters, int decimals) {
    const int places = std::clamp(decimals, 0, 6);
    std::int64_t scale = 1;
    for (int i = 0; i < places; ++i) {
        scale *= 10;
    }
    const std::int64_t scaled = std::llround(meters * static_cast<double>(scale));
    const std::uint64_t magnitude =
        scaled < 0 ? static_cast<std::uint64_t>(-(scaled + 1)) + 1u : static_cast<std::uint64_t>(scaled);
    std::string out = scaled < 0 ? std::string(kMinus) : std::string();
    out += grouped(magnitude / static_cast<std::uint64_t>(scale));
    if (places > 0) {
        std::string fraction = std::to_string(magnitude % static_cast<std::uint64_t>(scale));
        out += ',' + std::string(static_cast<std::size_t>(places) - fraction.size(), '0') + fraction;
    }
    return out;
}

} // namespace terrain::unreal
