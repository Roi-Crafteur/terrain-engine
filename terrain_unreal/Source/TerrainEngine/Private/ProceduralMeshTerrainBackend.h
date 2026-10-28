#pragma once

#include "Bridge/tile_blend_atlas.hpp"
#include "CoreMinimal.h"
#include "TerrainMeshBackend.h"

#include <optional>

#include "ProceduralMeshTerrainBackend.generated.h"

class UProceduralMeshComponent;
class UTexture2D;

/**
 * One UProceduralMeshComponent per tile (section 0), placed in world space at the origin of the
 * tile: its vertices stay small floats relative to it, the component placement is a double
 * (Large World Coordinates). The LOD 0 tiles get a complex-as-simple collision.
 *
 * When the material of the tiles is an instance with the texture parameter TileBlendTexels (the
 * temporary terrain material), the surface blend of every tile goes to one texture of float
 * texels, a slot per tile (Bridge/tile_blend_atlas.hpp); the custom primitive data of its
 * component give the material its slot (index 0, -1 for none) and the centimeters between its
 * texels (index 1).
 */
UCLASS(Transient)
class UProceduralMeshTerrainBackend : public UTerrainMeshBackend
{
	GENERATED_BODY()

public:
	virtual void Initialize(AActor& Owner, UMaterialInterface* Material,
		bool bAsyncCollisionCooking) override;
	virtual void SetTile(const terrain::unreal::TileUpload& Tile) override;
	virtual void MoveTile(const terrain::TileKey& Key,
		const terrain::unreal::UnrealPoint& Origin) override;
	virtual void RemoveTile(const terrain::TileKey& Key) override;
	virtual void RemoveAllTiles() override;
	virtual int32 GetTileCount() const override { return Tiles.Num(); }

private:
	UProceduralMeshComponent* CreateTileComponent();
	void CreateBlendTexture(UMaterialInterface* Material);
	void UploadBlend(UProceduralMeshComponent& Component, const terrain::unreal::TileUpload& Tile);

	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TileMaterial;

	UPROPERTY()
	TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Tiles;

	UPROPERTY()
	TObjectPtr<UTexture2D> BlendTexture;

	std::optional<terrain::unreal::BlendAtlas> BlendAtlas; // with BlendTexture only

	bool bAsyncCooking = false;
};
