#pragma once

#include "CoreMinimal.h"
#include "TerrainMeshBackend.h"

#include "ProceduralMeshTerrainBackend.generated.h"

class UProceduralMeshComponent;

/**
 * One UProceduralMeshComponent per tile (section 0), placed in world space at the origin of the
 * tile: its vertices stay small floats relative to it, the component placement is a double
 * (Large World Coordinates). The LOD 0 tiles get a complex-as-simple collision.
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

	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TileMaterial;

	UPROPERTY()
	TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Tiles;

	bool bAsyncCooking = false;
};
