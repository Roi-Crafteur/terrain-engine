#pragma once

#include "CoreMinimal.h"

#include "Bridge/map_tile_cache.hpp"

#include <memory>

class UTexture2D;

// The textures of the tiles of the map (agent 35): one transient RGBA texture of kMapTilePixels
// square per tile shown, filled from the bytes terrain_core rendered.
namespace TerrainMapTextures
{
	// A texture for the tiles, kept by its owner (a UPROPERTY): sRGB, bilinear, clamped.
	UTexture2D* CreateTileTexture(UObject* Outer, int32 Slot);

	// The image of the tile copied to the texture on the render thread; the tile is kept alive until
	// then (no copy of its bytes on the game thread).
	void UploadTile(UTexture2D& Texture, std::shared_ptr<const terrain::unreal::MapTile> Tile);
} // namespace TerrainMapTextures
