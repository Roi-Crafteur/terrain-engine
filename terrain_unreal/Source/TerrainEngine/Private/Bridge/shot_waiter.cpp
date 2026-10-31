#include "shot_waiter.hpp"

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
    return text;
}

} // namespace terrain::unreal
