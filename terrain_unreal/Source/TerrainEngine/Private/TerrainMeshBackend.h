#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Bridge/tile_streamer.hpp"
#include "TerrainWorld.h"

#include "TerrainMeshBackend.generated.h"

class AActor;
class UMaterialInterface;

/**
 * How the tiles of an ATerrainWorld become Unreal meshes: one implementation per mesh type
 * (ETerrainMeshType), so that they can be compared by measure. The tiles come as the Bridge
 * converts them (centimeters, Z up, Unreal triangle order, a color per vertex), each placed in
 * world space at its origin. A UObject, so that the garbage collector sees the components.
 */
UCLASS(Abstract, Transient)
class UTerrainMeshBackend : public UObject
{
	GENERATED_BODY()

public:
	// The meshes are components of Owner, drawn with Material (none: the default material).
	virtual void Initialize(AActor& /*Owner*/, UMaterialInterface* /*Material*/,
		bool /*bAsyncCollisionCooking*/)
	{
	}

	// Shows the tile: creates its mesh, or replaces the one shown under its key.
	virtual void SetTile(const terrain::unreal::TileUpload& /*Tile*/) {}

	// Removes the mesh shown under this key; nothing for a key not shown.
	virtual void RemoveTile(const terrain::TileKey& /*Key*/) {}

	virtual void RemoveAllTiles() {}

	virtual int32 GetTileCount() const { return 0; }
};

// The backend of the mesh type, initialized. The placeholders fall back to ProceduralMesh with a
// warning.
UTerrainMeshBackend* CreateTerrainMeshBackend(ETerrainMeshType Type, AActor& Owner,
	UMaterialInterface* Material, bool bAsyncCollisionCooking);

// Key of a tile in the maps of the backends.
inline FIntVector TileMapKey(const terrain::TileKey& Key)
{
	return FIntVector(Key.origin.x, Key.origin.z, Key.lod);
}
