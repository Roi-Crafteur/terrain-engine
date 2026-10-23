#include "terrain_session.hpp"

#include "terrain/generation/caves.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/storage/planet_file.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

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

// The hydrology of the planet level of the file, or none without a file.
std::shared_ptr<const Hydrology> hydrology(const SessionParams& params) {
    if (params.planetFile.empty()) {
        return nullptr;
    }
    const std::filesystem::path file(reinterpret_cast<const char8_t*>(params.planetFile.c_str()));
    const Heightfield relief(params.seed, HeightfieldParams{}, params.world);
    auto planet = std::make_shared<const PlanetLevel>(
        openPlanetLevel(file, params.seed, HeightfieldParams{}, PlanetParams{}, params.world));
    return std::make_shared<const Hydrology>(params.seed, relief, std::move(planet));
}

StreamParams streamParams(const SessionParams& params) {
    StreamParams stream = params.stream;
    stream.world = params.world;
    return stream;
}

} // namespace

TerrainSession::TerrainSession(const SessionParams& params)
    : tiles_(params.seed, HeightfieldParams{}, EcologyParams{}, caveParams(params), params.world,
             hydrology(params)),
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
