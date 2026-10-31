#pragma once

#include "CoreMinimal.h"

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

// The teleport of the dev map: the view at the core point (X, Z), its heading and pitch (degrees),
// its eye at the altitude Eye or Eye meters above the ground or the water; true once the pawn is
// placed. ATerrainWorld::TeleportView of the tools of agent 36 when they are there; unset, the map
// says so and stays.
using FTerrainMapTeleport =
	TFunction<bool(double X, double Z, double Heading, double Pitch, double Eye, bool bEyeAboveGround)>;

// A teleport asked for: the screen "Loading the terrain" is shown one frame first.
struct FTerrainMapPendingTeleport
{
	double X = 0.0;
	double Z = 0.0;
	double Heading = 0.0;
	double Pitch = 0.0;
	double Eye = 1.8;
	bool bEyeAboveGround = true;
	FString What;
	int32 FramesLeft = 1;
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
	std::unique_ptr<terrain::unreal::MapPlaceSource> Places;
	std::unique_ptr<terrain::unreal::MapReveal> Reveal;
	FTerrainMapTeleport Teleport;

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

	std::optional<FTerrainMapPendingTeleport> Pending;
	FString Message;
};
