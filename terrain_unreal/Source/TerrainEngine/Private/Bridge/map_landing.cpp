#include "map_landing.hpp"

#include "place_pose.hpp"

#include "terrain/generation/heightfield.hpp"

#include <cstdio>
#include <limits>

namespace terrain::unreal {

MapLanding clickLanding(double x, double z, double radius, const GroundHeight& height,
                        const GroundWet& wet) {
    MapLanding landing;
    landing.x = x;
    landing.z = z;
    landing.clickX = x;
    landing.clickZ = z;
    landing.clickWet = wet(x, z);
    if (!landing.clickWet) {
        return landing;
    }
    WalkableSearch search;
    search.radius = radius;
    search.maxSlope = (std::numeric_limits<double>::max)(); // any dry column: the nearest
    const WalkableCandidates dry = walkableCandidates(x, z, search, height, wet, 1);
    landing.reason = dry.reason;
    if (dry.spots.empty()) {
        landing.kind = MapLandingKind::OverWater;
        return landing;
    }
    landing.x = dry.spots.front().x;
    landing.z = dry.spots.front().z;
    return landing;
}

MapLanding clickLandingOn(const Heightfield& field, double x, double z, double radius) {
    const GroundHeight height = [&field](double px, double pz) {
        return static_cast<double>(field.heightAt(px, pz));
    };
    const GroundWet wet = [&field](double px, double pz) {
        return columnHasWater(field.sampleAt(px, pz));
    };
    return clickLanding(x, z, radius, height, wet);
}

std::optional<MapLanding> landingFallback(const MapLanding& landing) {
    if (landing.kind != MapLandingKind::Land || !landing.clickWet) {
        return std::nullopt;
    }
    MapLanding back = landing;
    back.kind = MapLandingKind::OverWater;
    back.x = landing.clickX;
    back.z = landing.clickZ;
    return back;
}

std::string landingText(const MapLanding& landing, double radius) {
    char text[200];
    if (landing.kind == MapLandingKind::OverWater) {
        std::snprintf(text, sizeof text,
                      "over water (no dry ground within %.0f m): the view of the tools 1.8 m above "
                      "the water; Terrain.Pawn over land",
                      radius);
    } else if (landing.clickWet) {
        std::snprintf(text, sizeof text,
                      "shallow water: the pawn of the game goes down near the dry ground at x "
                      "%.2f, z %.2f",
                      landing.x, landing.z);
    } else {
        std::snprintf(text, sizeof text, "the pawn of the game goes down near x %.2f, z %.2f",
                      landing.x, landing.z);
    }
    return text;
}

} // namespace terrain::unreal
