#pragma once

#include "CoreMinimal.h"

#include "Bridge/map_landing.hpp"
#include "Bridge/map_places_port.hpp"
#include "Bridge/map_reveal.hpp"
#include "Bridge/map_tile_cache.hpp"
#include "Bridge/map_view.hpp"
#include "terrain/map/map_frame.hpp"
#include "terrain/map/map_probe.hpp"
#include "terrain/map/map_source.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SWidget;
class STerrainDevMap;
class STerrainMapCanvas;

namespace terrain::unreal
{
	class TerrainSession;
}

// A click (or a point typed) on its way (TerrainDevMapLanding.cpp, only with TERRAIN_DEV_TOOLS):
// its landing decided on the thread of the map (clickLanding), the view of the tools put there
// (ATerrainWorld::TeleportView), its load waited for, the pawn of the game given back
// (ATerrainWorld::ReturnToGamePawn), and a second later the line of the check in the log.
struct FTerrainMapClick
{
	enum class EStep : uint8
	{
		Deciding, // the landing asked to the thread of the map
		Loading,  // the view of the tools put at the dry ground, its load waited for
		Watching, // the pawn of the game given back, the check a second later
	};
	EStep Step = EStep::Deciding;
	double X = 0.0; // the point clicked, its heading and pitch
	double Z = 0.0;
	double Heading = 0.0;
	double Pitch = 0.0;
	FString What;
	terrain::unreal::MapLanding Landing; // once decided
	double Since = 0.0;                  // FPlatformTime::Seconds of the step
	int32 Frames = 0;                    // ticks since the step began
};

// The texture holding a tile of the map: its slot in UTerrainDevMapComponent::TileTextures and the
// frame it was last drawn in.
struct FTerrainMapTileTexture
{
	int32 Slot = INDEX_NONE;
	uint64 LastFrame = 0;
};

// The terrain_core and Bridge side of the map of a playing world. Members are destroyed in reverse
// order: the threads of the cache stop before the source they read goes away.
struct FTerrainDevMapState
{
	// The session the map reads (the world playing): a new session (Terrain.Seed) starts the map
	// again.
	const terrain::unreal::TerrainSession* Session = nullptr;
	terrain::WorldSize World;
	std::unique_ptr<terrain::MapWorldSource> Source;
	std::unique_ptr<terrain::unreal::MapTileCache> Cache;
	// The places of the tools of agent 36 (Terrain.PlacesFile), read again when the dev map opens
	// and after a place is added; their copy drawn on the map.
	std::unique_ptr<terrain::unreal::MapPlaceSource> Places;
	std::vector<terrain::dev::Place> PlacesShown;
	std::unique_ptr<terrain::unreal::MapReveal> Reveal;

	// The textures of the tiles and those waiting for one.
	std::map<terrain::MapTileKey, FTerrainMapTileTexture> Textures;
	std::deque<terrain::MapTileKey> ToUpload;
	// The tiles the views ask for this frame, the dev map first.
	std::vector<terrain::MapTileKey> Wanted;
	TArray<int32> FreeSlots;
	uint64 Frame = 0;

	// The player: its canonical core point, its heading and pitch (degrees).
	bool bHasPlayer = false;
	terrain::unreal::WorldPoint Player;
	double Heading = 0.0;
	double Pitch = 0.0;

	// The dev map: its view (centered on the player the first time it opens) and its widgets.
	terrain::unreal::MapView View;
	bool bViewCentered = false;
	TSharedPtr<STerrainDevMap> DevMap;
	TSharedPtr<SWidget> Minimap;
	TSharedPtr<STerrainMapCanvas> MinimapCanvas;
	TSharedPtr<SWidget> Loading;

	// Under the cursor of the dev map: the point asked and the last probe received.
	std::optional<terrain::unreal::WorldPoint> Hover;
	std::optional<terrain::unreal::WorldPoint> HoverAsked;
	std::optional<terrain::MapProbe> Probe;

	std::optional<FTerrainMapClick> Click;
	FString Message;
};
