#include "terrain_session.hpp"

#include "terrain/generation/caves.hpp"

#include <optional>

namespace terrain::unreal {

namespace {

std::optional<CaveParams> caveParams(const SessionParams& params) {
    if (!params.caves) {
        return std::nullopt;
    }
    CaveParams caves;
    if (params.caveRegions) {
        caves.districts = CaveDistrictParams{};
    }
    return caves;
}

StreamParams streamParams(const SessionParams& params) {
    StreamParams stream = params.stream;
    stream.world = params.world;
    return stream;
}

} // namespace

TerrainSession::TerrainSession(const SessionParams& params)
    : tiles_(params.seed, HeightfieldParams{}, EcologyParams{}, caveParams(params), params.world),
      timings_(std::make_shared<TileTimings>()),
      streamer_(timedGenerator(tileGenerator(tiles_), timings_), streamParams(params), timings_) {
}

UnrealFrame TerrainSession::frameAt(double x, double z) const {
    return {{x, static_cast<double>(tiles_.heightfield().heightAt(x, z)), z}};
}

double TerrainSession::groundZ(const UnrealFrame& frame, const UnrealPoint& point) const {
    const CorePoint core = toCore(frame, point);
    const double ground = tiles_.heightfield().heightAt(core.x, core.z);
    return toUnreal(frame, {core.x, ground, core.z}).z;
}

} // namespace terrain::unreal
