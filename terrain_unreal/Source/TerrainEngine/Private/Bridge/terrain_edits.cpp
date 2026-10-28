#include "terrain_edits.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace terrain::unreal {

namespace {

constexpr double kStep = 8.0;            // between the heights of a razed square
constexpr double kMaxRadius = 4096.0;    // kMaxEditRadius, meters
constexpr double kMaxSide = 65535.0;     // the blocks of a box along an axis
constexpr double kHeadroomMeters = 10.0; // over the highest ground of a razed square

// A positive finite number, the whole text (strtod: from_chars of doubles is missing from some
// standard libraries).
std::optional<double> positive(const std::string& text) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value) ||
        value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

int floorInt(double v) {
    return static_cast<int>(std::floor(v));
}

} // namespace

std::optional<GroundEditCommand> parseGroundEdit(std::span<const std::string> args,
                                                 std::string& error) {
    GroundEditCommand command;
    if (args.empty()) {
        error = "expected raze, blast or heap";
        return std::nullopt;
    }
    if (args[0] == "raze") {
        command.kind = GroundEdit::Raze;
    } else if (args[0] == "blast") {
        command.kind = GroundEdit::Blast;
    } else if (args[0] == "heap") {
        command.kind = GroundEdit::Heap;
    } else {
        error = "unknown edit '" + args[0] + "': expected raze, blast or heap";
        return std::nullopt;
    }
    const std::size_t most = command.kind == GroundEdit::Raze ? 4 : 3;
    const std::optional<double> distance = args.size() > 1 ? positive(args[1]) : std::nullopt;
    const std::optional<double> size = args.size() > 2 ? positive(args[2]) : std::nullopt;
    const std::optional<double> depth = args.size() > 3 ? positive(args[3]) : std::nullopt;
    if (!distance || !size || args.size() > most || (args.size() == 4 && !depth)) {
        error = command.kind == GroundEdit::Raze
                    ? "expected raze <distance m> <side m> [depth m], positive numbers"
                    : "expected " + args[0] + " <distance m> <radius m>, positive numbers";
        return std::nullopt;
    }
    const double limit = command.kind == GroundEdit::Raze ? kMaxSide : kMaxRadius;
    if (*size > limit) {
        error = "the size is beyond what one edit holds";
        return std::nullopt;
    }
    command.distance = *distance;
    command.size = *size;
    command.depth = depth.value_or(command.depth);
    return command;
}

CorePoint pointAhead(const UnrealFrame& frame, const UnrealPoint& camera, double forwardX,
                     double forwardY, double distance) {
    const CorePoint eye = toCore(frame, camera);
    const double length = std::hypot(forwardX, forwardY);
    if (!(length > 0.0)) {
        return {eye.x, 0.0, eye.z};
    }
    // Unreal X and Y are the core x and z (unreal_frame.hpp).
    return {eye.x + forwardX / length * distance, 0.0, eye.z + forwardY / length * distance};
}

VolumeEdit groundEditAt(const Heightfield& field, const GroundEditCommand& command, double x,
                        double z) {
    if (command.kind != GroundEdit::Raze) {
        const auto radius = static_cast<std::uint16_t>(std::lround(command.size * 16.0));
        const double ground = field.heightAt(x, z);
        const double below = command.kind == GroundEdit::Heap ? command.size / 3.0 : 0.0;
        return {command.kind == GroundEdit::Heap ? EditShape::Sphere : EditShape::Explosion,
                command.kind == GroundEdit::Heap ? EditMode::Add : EditMode::Remove,
                {floorInt(x), floorInt(ground - below), floorInt(z)},
                {},
                radius};
    }
    const double half = command.size / 2.0;
    double low = field.heightAt(x, z);
    double high = low;
    for (double dz = -half; dz <= half; dz += kStep) {
        for (double dx = -half; dx <= half; dx += kStep) {
            const double ground = field.heightAt(x + dx, z + dz);
            low = std::min(low, ground);
            high = std::max(high, ground);
        }
    }
    const int bottom = floorInt(low - command.depth);
    const double tall = std::min(std::ceil(high + kHeadroomMeters) - bottom, kMaxSide);
    const auto side = static_cast<std::uint16_t>(std::lround(command.size));
    return {EditShape::Box,
            EditMode::Remove,
            {floorInt(x - half), bottom, floorInt(z - half)},
            {side, static_cast<std::uint16_t>(tall), side},
            0};
}

} // namespace terrain::unreal
