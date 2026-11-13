#pragma once

#include "Bridge/terrain_session.hpp"

#include <string>

// Terrain.Flags of the tools of the developers (agent 51, Bridge/dev_flags.hpp): lays the flags it
// set over Params (those of the actor) for the next Plays of this editor, their planet file under
// Saved/TerrainEngine/dev, and returns "Terrain.Flags"; "" when none are set, and always without
// TERRAIN_DEV_TOOLS (never in Shipping). Defined with the hooks of ATerrainWorld for the tools
// (TerrainWorldDev.cpp).
std::string ApplyTerrainDevFlags(terrain::unreal::SessionParams& Params);
