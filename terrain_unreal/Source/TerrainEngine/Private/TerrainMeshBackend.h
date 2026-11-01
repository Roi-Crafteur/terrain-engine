#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Bridge/dev_commands.hpp"
#include "Bridge/tile_streamer.hpp"
#include "Templates/Function.h"
#include "TerrainWorld.h"

#include <cstdint>
#include <vector>

#include "TerrainMeshBackend.generated.h"

class AActor;
class UMaterialInterface;

// Which vertices of the mesh of a tile (centimeters from its origin, Origin the world location of
// its component) lie off the heightmap: the mode roche of the tools of the developers.
using FTerrainOffHeightmap = TFunction<std::vector<std::uint8_t>(
	const terrain::unreal::UnrealMesh& Mesh, int32 Lod, const FVector& Origin)>;

/**
 * How the tiles of an ATerrainWorld become Unreal meshes: one implementation per mesh type
 * (ETerrainMeshType), so that they can be compared by measure. The tiles come as the Bridge
 * converts them (centimeters, Z up, Unreal triangle order, a color per vertex), each placed in
 * world space at the origin of its image nearest to the player, under its canonical key. A
 * UObject, so that the garbage collector sees the components.
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

	// Moves the mesh shown under this key to another image of its tile (whole worlds away),
	// unchanged otherwise; nothing for a key not shown.
	virtual void MoveTile(const terrain::TileKey& /*Key*/,
		const terrain::unreal::UnrealPoint& /*Origin*/)
	{
	}

	// Removes the mesh shown under this key; nothing for a key not shown.
	virtual void RemoveTile(const terrain::TileKey& /*Key*/) {}

	virtual void RemoveAllTiles() {}

	// The materials of the water of the tiles (agent 27): the surface of their lakes and rivers,
	// the curtains and foam of their falls (Bridge/tile_water_mesh.hpp, Bridge/fall_curtains.hpp).
	// None: that water is not drawn. For the tiles set from now on.
	virtual void SetWaterMaterials(UMaterialInterface* /*Water*/, UMaterialInterface* /*Falls*/) {}

	// Shows or hides the water of every tile, and of the tiles to come (Terrain.Water).
	virtual void SetWaterVisible(bool /*bVisible*/) {}

	virtual int32 GetTileCount() const { return 0; }

	// The tools of the developers (Terrain.View, agent 36). The vertices of the ground of every
	// tile, and of the tiles to come, take the colors of a view mode (Bridge/debug_colors.hpp)
	// and Material (the vertex color material of Unreal); ViewMode::Normal and the modes that do
	// not color the vertices give each tile its own colors and material back. OffHeightmap tells
	// the vertices of a mesh (centimeters from its origin, the world location of its component)
	// off the heightmap, for the mode roche.
	virtual void SetDebugLook(terrain::unreal::ViewMode /*Mode*/, UMaterialInterface* /*Material*/,
		FTerrainOffHeightmap /*OffHeightmap*/)
	{
	}

	// Shows or hides the ground of every tile, and of the tiles to come (Terrain.View eau).
	virtual void SetGroundVisible(bool /*bVisible*/) {}
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
