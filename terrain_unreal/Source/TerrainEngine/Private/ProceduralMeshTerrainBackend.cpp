#include "ProceduralMeshTerrainBackend.h"

#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TerrainEngineLog.h"
#include "TextureResource.h"

#include <cstddef>
#include <vector>

namespace
{
	// The texture of the surface blend of the tiles in their material, and its slots per side:
	// 64 x 64 slots (320 x 320 texels, 1.6 MB), many more than the tiles shown at once.
	const FName BlendTexelsParameter(TEXT("TileBlendTexels"));
	constexpr int32 BlendSlotsPerSide = 64;
	constexpr int32 BlendTexelBytes = terrain::unreal::kBlendTexelFloats * sizeof(float);
	// The custom primitive data of a tile: its slot, and the centimeters between its texels.
	constexpr int32 BlendSlotData = 0;
	constexpr int32 BlendPitchData = 1;
	// The sections of the water of a tile.
	constexpr int32 WaterSection = 1;
	constexpr int32 FallsSection = 2;

	TArray<FVector2D> ToUVs(const std::vector<terrain::unreal::UnrealUV>& UVs)
	{
		TArray<FVector2D> Out;
		Out.Reserve(static_cast<int32>(UVs.size()));
		for (const terrain::unreal::UnrealUV& UV : UVs)
		{
			Out.Emplace(UV.u, UV.v);
		}
		return Out;
	}

	TArray<FVector> ToVectors(const std::vector<terrain::unreal::UnrealVector3f>& Vectors)
	{
		TArray<FVector> Out;
		Out.Reserve(static_cast<int32>(Vectors.size()));
		for (const terrain::unreal::UnrealVector3f& Vector : Vectors)
		{
			Out.Emplace(Vector.x, Vector.y, Vector.z);
		}
		return Out;
	}
} // namespace

void UProceduralMeshTerrainBackend::Initialize(AActor& Owner, UMaterialInterface* Material,
	bool bAsyncCollisionCooking)
{
	OwnerActor = &Owner;
	TileMaterial = Material;
	bAsyncCooking = bAsyncCollisionCooking;
	CreateBlendTexture(Material);
}

void UProceduralMeshTerrainBackend::CreateBlendTexture(UMaterialInterface* Material)
{
	UMaterialInstanceDynamic* Look = Cast<UMaterialInstanceDynamic>(Material);
	UTexture* Unused = nullptr;
	if (Look == nullptr ||
		!Look->GetTextureParameterValue(FHashedMaterialParameterInfo(BlendTexelsParameter), Unused))
	{
		return;
	}
	BlendAtlas.emplace(BlendSlotsPerSide);
	const int32 Side = BlendAtlas->textureSide();
	BlendTexture = UTexture2D::CreateTransient(Side, Side, PF_A32B32G32R32F, TEXT("TileBlendTexels"));
	BlendTexture->Filter = TF_Nearest;
	BlendTexture->SRGB = false;
	BlendTexture->NeverStream = true;
	FTexture2DMipMap& Mip = BlendTexture->GetPlatformData()->Mips[0];
	void* Texels = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memzero(Texels, static_cast<SIZE_T>(Side) * Side * BlendTexelBytes); // no tile
	Mip.BulkData.Unlock();
	BlendTexture->UpdateResource();
	Look->SetTextureParameterValue(BlendTexelsParameter, BlendTexture);
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: surface blend of the tiles in %d x %d slots (%d x %d texels)"),
		BlendSlotsPerSide, BlendSlotsPerSide, Side, Side);
}

// The texels of the tile to its slot of the texture; its slot and pitch to its component.
void UProceduralMeshTerrainBackend::UploadBlend(UProceduralMeshComponent& Component,
	const terrain::unreal::TileUpload& Tile)
{
	if (!BlendAtlas.has_value())
	{
		return;
	}
	const bool bHasBlend = Tile.blend.size() ==
		static_cast<std::size_t>(terrain::kTileBlendTexels * terrain::kTileBlendTexels);
	const std::optional<int> Slot = bHasBlend ? BlendAtlas->place(Tile.key) : std::nullopt;
	Component.SetCustomPrimitiveDataFloat(BlendSlotData, Slot ? static_cast<float>(*Slot) : -1.0f);
	Component.SetCustomPrimitiveDataFloat(BlendPitchData,
		static_cast<float>(terrain::unreal::blendPitchMeters(Tile.key.lod) * 100.0));
	if (!Slot)
	{
		BlendAtlas->release(Tile.key);
		if (bHasBlend)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: no slot for the surface blend "
				"of a tile (%d tiles hold one): the tile takes the surface of the horizon"),
				static_cast<int32>(BlendAtlas->size()));
		}
		return;
	}
	const terrain::unreal::BlendAtlas::Corner Corner = BlendAtlas->corner(*Slot);
	auto* Texels = new std::vector<float>(terrain::unreal::blendRegion(Tile.blend));
	auto* Rect = new FUpdateTextureRegion2D(static_cast<uint32>(Corner.x),
		static_cast<uint32>(Corner.y), 0, 0, terrain::kTileBlendTexels, terrain::kTileBlendTexels);
	BlendTexture->UpdateTextureRegions(0, 1, Rect,
		static_cast<uint32>(terrain::kTileBlendTexels * BlendTexelBytes),
		static_cast<uint32>(BlendTexelBytes), reinterpret_cast<uint8*>(Texels->data()),
		[Texels](uint8*, const FUpdateTextureRegion2D* Regions)
		{
			delete Texels;
			delete Regions;
		});
}

void UProceduralMeshTerrainBackend::SetTile(const terrain::unreal::TileUpload& Tile)
{
	if (Tile.mesh.empty())
	{
		RemoveTile(Tile.key);
		return;
	}
	const FIntVector Key = TileMapKey(Tile.key);
	TObjectPtr<UProceduralMeshComponent>* Found = Tiles.Find(Key);
	UProceduralMeshComponent* Component = Found != nullptr ? Found->Get() : nullptr;
	if (Component == nullptr)
	{
		Component = CreateTileComponent();
		Tiles.Add(Key, Component);
	}

	const terrain::unreal::UnrealMesh& Mesh = Tile.mesh;
	const TArray<FVector> Vertices = ToVectors(Mesh.positions);
	const TArray<FVector> Normals = ToVectors(Mesh.normals);
	TArray<FColor> Colors;
	Colors.Reserve(static_cast<int32>(Mesh.colors.size()));
	for (const terrain::unreal::VertexColor& Color : Mesh.colors)
	{
		Colors.Emplace(Color.r, Color.g, Color.b, Color.a);
	}
	// The water over each vertex in UV channel 0: (depth in meters, 1 under water / 0 dry), its
	// level at Z + 100 depth (Bridge/tile_mesh_conversion.hpp).
	TArray<FVector2D> Water;
	Water.Reserve(static_cast<int32>(Mesh.water.size()));
	for (const terrain::unreal::VertexWater& Over : Mesh.water)
	{
		Water.Emplace(Over.depth, Over.wet);
	}
	TArray<int32> Triangles;
	Triangles.Append(Mesh.triangles.data(), static_cast<int32>(Mesh.triangles.size()));

	Component->SetWorldLocation(FVector(Tile.origin.x, Tile.origin.y, Tile.origin.z));
	Component->CreateMeshSection(0, Vertices, Triangles, Normals, Water, Colors,
		TArray<FProcMeshTangent>(), Tile.collision);
	Component->SetCollisionEnabled(
		Tile.collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	SetWaterSection(*Component, WaterSection, Tile.water, WaterMaterial);
	SetWaterSection(*Component, FallsSection, Tile.falls, FallsMaterial);
	UploadBlend(*Component, Tile);
	// The tools of the developers (ProceduralMeshTerrainLooks.cpp): a new mesh in their mode.
	Component->SetMeshSectionVisible(0, bGroundVisible);
	OwnColors.Remove(Key);
	if (ViewLook != terrain::unreal::ViewMode::Normal)
	{
		ApplyLook(*Component, Key);
	}
}

// A section of the water of a tile, without collision; cleared when the tile has none of it (a
// tile set again after an edit) or without its material.
void UProceduralMeshTerrainBackend::SetWaterSection(UProceduralMeshComponent& Component,
	int32 Section, const terrain::unreal::UnrealWaterMesh& Water, UMaterialInterface* Material) const
{
	if (Water.empty() || Material == nullptr)
	{
		if (Section < Component.GetNumSections())
		{
			Component.ClearMeshSection(Section);
		}
		return;
	}
	TArray<int32> Triangles;
	Triangles.Append(Water.triangles.data(), static_cast<int32>(Water.triangles.size()));
	Component.CreateMeshSection(Section, ToVectors(Water.positions), Triangles,
		ToVectors(Water.normals), ToUVs(Water.uv0), ToUVs(Water.uv1), ToUVs(Water.uv2),
		TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Component.SetMaterial(Section, Material);
	Component.SetMeshSectionVisible(Section, bWaterVisible);
}

void UProceduralMeshTerrainBackend::SetWaterMaterials(UMaterialInterface* Water,
	UMaterialInterface* Falls)
{
	WaterMaterial = Water;
	FallsMaterial = Falls;
}

void UProceduralMeshTerrainBackend::SetWaterVisible(bool bVisible)
{
	bWaterVisible = bVisible;
	for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& Tile : Tiles)
	{
		UProceduralMeshComponent* Component = Tile.Value.Get();
		for (const int32 Section : {WaterSection, FallsSection})
		{
			if (Component != nullptr && Section < Component->GetNumSections())
			{
				Component->SetMeshSectionVisible(Section, bVisible);
			}
		}
	}
}

void UProceduralMeshTerrainBackend::MoveTile(const terrain::TileKey& Key,
	const terrain::unreal::UnrealPoint& Origin)
{
	const TObjectPtr<UProceduralMeshComponent>* Found = Tiles.Find(TileMapKey(Key));
	if (Found != nullptr && *Found != nullptr)
	{
		(*Found)->SetWorldLocation(FVector(Origin.x, Origin.y, Origin.z));
	}
}

void UProceduralMeshTerrainBackend::RemoveTile(const terrain::TileKey& Key)
{
	TObjectPtr<UProceduralMeshComponent> Component;
	if (Tiles.RemoveAndCopyValue(TileMapKey(Key), Component) && Component != nullptr)
	{
		Component->DestroyComponent();
	}
	OwnColors.Remove(TileMapKey(Key));
	if (BlendAtlas.has_value())
	{
		BlendAtlas->release(Key);
	}
}

void UProceduralMeshTerrainBackend::RemoveAllTiles()
{
	for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& Tile : Tiles)
	{
		if (Tile.Value != nullptr)
		{
			Tile.Value->DestroyComponent();
		}
	}
	Tiles.Empty();
	OwnColors.Empty();
	if (BlendAtlas.has_value())
	{
		BlendAtlas->clear();
	}
}

UProceduralMeshComponent* UProceduralMeshTerrainBackend::CreateTileComponent()
{
	UProceduralMeshComponent* Component =
		NewObject<UProceduralMeshComponent>(OwnerActor, NAME_None, RF_Transient);
	Component->bUseAsyncCooking = bAsyncCooking;
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// In world space whatever the transform of the actor: the frame of the core places the tiles.
	Component->SetUsingAbsoluteLocation(true);
	Component->SetUsingAbsoluteRotation(true);
	Component->SetUsingAbsoluteScale(true);
	Component->SetupAttachment(OwnerActor->GetRootComponent());
	Component->RegisterComponent();
	if (TileMaterial != nullptr)
	{
		Component->SetMaterial(0, TileMaterial);
	}
	return Component;
}
