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
 *
 * The water of a tile (agent 27) is drawn by two more sections of its component, without
 * collision, already at the level of the water: section 1 the surface of its lakes and rivers
 * (the water material), section 2 the curtains and foam of its falls (the falls material).
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
	virtual void SetWaterMaterials(UMaterialInterface* Water, UMaterialInterface* Falls) override;
	virtual void SetWaterVisible(bool bVisible) override;
	// ProceduralMeshTerrainLooks.cpp: the colors change through UpdateMeshSection, colors only (no
	// collision cooked again); each tile keeps its own colors meanwhile.
	virtual void SetDebugLook(terrain::unreal::ViewMode Mode, UMaterialInterface* Material,
		FTerrainOffHeightmap OffHeightmap) override;
	virtual void SetGroundVisible(bool bVisible) override;

private:
	void ApplyLook(UProceduralMeshComponent& Component, const FIntVector& Key);
	UProceduralMeshComponent* CreateTileComponent();
	void CreateBlendTexture(UMaterialInterface* Material);
	void UploadBlend(UProceduralMeshComponent& Component, const terrain::unreal::TileUpload& Tile);
	void SetWaterSection(UProceduralMeshComponent& Component, int32 Section,
		const terrain::unreal::UnrealWaterMesh& Water, UMaterialInterface* Material) const;

	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TileMaterial;

	UPROPERTY()
	TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Tiles;

	UPROPERTY()
	TObjectPtr<UTexture2D> BlendTexture;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FallsMaterial;

	std::optional<terrain::unreal::BlendAtlas> BlendAtlas; // with BlendTexture only

	// The view mode of the tools, its material and the tiles recolored with their own colors.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> LookMaterial;

	terrain::unreal::ViewMode Look = terrain::unreal::ViewMode::Normal;
	FTerrainOffHeightmap OffHeightmapOf;
	TMap<FIntVector, TArray<FColor>> OwnColors;
	bool bGroundVisible = true;

	bool bAsyncCooking = false;
	bool bWaterVisible = true;
};
