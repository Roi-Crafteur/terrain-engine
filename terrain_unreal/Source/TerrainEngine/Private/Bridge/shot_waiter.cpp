#include "shot_waiter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace terrain::unreal {

StreamingState tileState(const TileStreamer& streamer) {
    StreamingState state;
    state.pendingTiles = streamer.manager().pendingCount();
    for (const TileRequest& request : streamer.manager().selection()) {
        ++state.selectedTiles;
        state.shownTiles += streamer.shown(request.key) ? 1 : 0;
    }
    return state;
}

bool settled(const StreamingState& state) {
    return state.pendingTiles == 0 && state.shownTiles == state.selectedTiles &&
           state.horizonShown && !state.horizonBusy && state.editsPending == 0;
}

ShotWaiter::ShotWaiter(const ShotWaitParams& params) : params_(params) {
    if (params.leastSeconds < 0.0 || params.settleSeconds < 0.0 ||
        params.timeoutSeconds < params.leastSeconds) {
        throw std::invalid_argument("ShotWaiter: times must be positive, the timeout the longest");
    }
}

ShotWait ShotWaiter::update(double elapsedSeconds, const StreamingState& state) {
    if (!settled(state)) {
        settledSince_ = -1.0;
    } else if (settledSince_ < 0.0) {
        settledSince_ = elapsedSeconds;
    }
    const bool steady = settledSince_ >= 0.0 &&
                        elapsedSeconds - settledSince_ >= params_.settleSeconds;
    if (elapsedSeconds >= params_.leastSeconds && steady) {
        return ShotWait::Ready;
    }
    return elapsedSeconds >= params_.timeoutSeconds ? ShotWait::TimedOut : ShotWait::Waiting;
}

std::string viewsFolderName(int year, int month, int day, int hour, int minute, int second) {
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d_%02d-%02d-%02d", year, month, day, hour,
                  minute, second);
    return text;
}

std::string viewFileName(std::size_t index, const std::string& place) {
    char number[24];
    std::snprintf(number, sizeof(number), "%02zu_", index);
    return number + place + ".png";
}

std::string viewsHeader(std::int64_t seed, const std::string& preset,
                        const std::string& planetFile, const std::string& rings) {
    return "# Terrain.Views: seed " + std::to_string(seed) + ", preset " + preset +
           ", planet file " + (planetFile.empty() ? std::string("none") : planetFile) +
           ", LOD rings " + rings + "\n";
}

namespace {

// The difference of two angles, degrees, brought into [0, 180].
double angleGap(double a, double b) {
    const double gap = std::fmod(std::abs(a - b), 360.0);
    return gap > 180.0 ? 360.0 - gap : gap;
}

std::string measuredText(const CaptureCheck& c) {
    char text[256];
    std::snprintf(text, sizeof(text),
                  "; camera yaw %.2f pitch %.2f fov %.1f, image %dx%d (viewport %dx%d, texture "
                  "%dx%d, read from the %s)",
                  c.camera.yaw, c.camera.pitch, c.fov, c.imageWidth, c.imageHeight,
                  c.viewportWidth, c.viewportHeight, c.textureWidth, c.textureHeight,
                  c.method.c_str());
    return text;
}

} // namespace

std::vector<std::string> captureWarnings(const ViewRecord& r) {
    std::vector<std::string> warnings;
    const CaptureCheck& c = r.capture;
    if (!c.measured) {
        return warnings;
    }
    if (angleGap(c.camera.yaw, r.pose.yaw) > 0.5 || angleGap(c.camera.pitch, r.pose.pitch) > 0.5) {
        warnings.push_back("the camera was not at the pose asked");
    }
    if (c.imageWidth > 0 && (c.imageWidth != c.textureWidth || c.imageHeight != c.textureHeight)) {
        warnings.push_back("the image is not the size of the view (a display scaled by Windows?)");
    }
    return warnings;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
pngSize(std::span<const std::uint8_t> bytes) {
    constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 24 || !std::equal(kSignature, kSignature + 8, bytes.begin()) ||
        bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R') {
        return std::nullopt;
    }
    const auto bigEndian = [&bytes](std::size_t at) {
        return (std::uint32_t{bytes[at]} << 24) | (std::uint32_t{bytes[at + 1]} << 16) |
               (std::uint32_t{bytes[at + 2]} << 8) | std::uint32_t{bytes[at + 3]};
    };
    const std::uint32_t width = bigEndian(16);
    const std::uint32_t height = bigEndian(20);
    if (width == 0 || height == 0) {
        return std::nullopt;
    }
    return std::make_pair(width, height);
}

std::string describeView(const ViewRecord& r) {
    char text[384];
    std::snprintf(text, sizeof(text),
                  "%s %s x %.2f z %.2f eye %.2f m yaw %.2f pitch %.2f: %s after %.1f s, %zu "
                  "tiles pending, %zu/%zu shown, horizon %s, %.1f fps%s",
                  r.file.c_str(), r.place.name.c_str(), r.pose.eye.x, r.pose.eye.z, r.pose.eye.y,
                  r.pose.yaw, r.pose.pitch, r.ready ? "ready" : "NOT READY (timeout)",
                  r.waitedSeconds, r.state.pendingTiles, r.state.shownTiles,
                  r.state.selectedTiles,
                  !r.state.horizonShown ? "not shown" : r.state.horizonBusy ? "busy" : "done",
                  r.framesPerSecond, r.planetMissing ? ", WITHOUT the planet file it needs" : "");
    std::string line = text;
    if (r.capture.measured) {
        line += measuredText(r.capture);
    }
    for (const std::string& warning : captureWarnings(r)) {
        line += "; WARNING: " + warning;
    }
    return line;
}

} // namespace terrain::unreal
