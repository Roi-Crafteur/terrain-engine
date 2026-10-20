#include "view_modes.hpp"

namespace viewer {

bool streamed(ViewMode mode) {
    return mode == ViewMode::Streaming || mode == ViewMode::Persistent;
}

bool underground(ViewMode mode) {
    return mode == ViewMode::Caves || mode == ViewMode::CaveRegions;
}

bool fastMoving(ViewMode mode) {
    return streamed(mode) || underground(mode);
}

const char* modeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::Cube:
        return "cube";
    case ViewMode::Sphere:
        return "sphere chunk";
    case ViewMode::Heightmap:
        return "heightmap chunk";
    case ViewMode::Caves:
        return "caves, lakes and speleothems (fly underground: the rock is seen through, the caves "
               "are not; Shift: fast)";
    case ViewMode::CaveRegions:
        return "cave regions (districts tint the rock, flora in the lush ones; fly underground "
               "as in 4; Shift: fast)";
    case ViewMode::Eroded:
        return "eroded region with rivers (1 unit = 50 m; Shift+5)";
    case ViewMode::Cliffs:
        return "cliffs region with rivers (1 unit = 12.5 m)";
    case ViewMode::Sculpted:
        return "cliffs with overhangs and arches (1 unit = 2.5 m)";
    case ViewMode::Streaming:
        return "streaming + LOD (tint: LOD 0 green, 1 yellow, 2 orange, 3 purple; Shift: fast)";
    case ViewMode::Persistent:
        return "save and reload (LMB dig, RMB build, P tree, O uproot, F5 save, F9 reload)";
    case ViewMode::Heightfield:
        return "heightfield";
    }
    return "";
}

Place placeOf(ViewMode mode) {
    switch (mode) {
    case ViewMode::Heightfield:
        return Place::Summit;
    case ViewMode::Caves:
        return Place::CaveWorld;
    case ViewMode::CaveRegions:
        return Place::CaveRegionWorld;
    case ViewMode::Eroded:
        return Place::ErodedModel;
    case ViewMode::Cliffs:
        return Place::CliffModel;
    case ViewMode::Sculpted:
        return Place::SculptedModel;
    case ViewMode::Streaming:
    case ViewMode::Persistent:
        return Place::StreamedWorld;
    default:
        return Place::ChunkOrigin;
    }
}

bool fivePressed(ModeKeys& keys, bool fiveDown) {
    const bool pressed = fiveDown && !keys.fiveDown;
    keys.fiveDown = fiveDown;
    return pressed;
}

ViewMode fiveMode(bool shift) {
    return shift ? ViewMode::Eroded : ViewMode::CaveRegions;
}

} // namespace viewer
