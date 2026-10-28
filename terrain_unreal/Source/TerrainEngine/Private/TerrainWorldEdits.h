#pragma once

struct FTerrainWorldState;

// Once per frame, before the tiles are streamed: the edits the edit thread of the session finished
// since the last frame have their tiles generated again and reach the far horizon, each logged
// with its cost (TerrainWorldEdits.cpp, Bridge/terrain_session.hpp).
void TickTerrainEdits(FTerrainWorldState& State);
