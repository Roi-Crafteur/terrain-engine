#include "ProceduralMeshTerrainBackend.h"

#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

void UProceduralMeshTerrainBackend::Initialize(AActor& Owner, UMaterialInterface* Material,
	bool bAsyncCollisionCooking)
{
	OwnerActor = &Owner;
	TileMaterial = Material;
	bAsyncCooking = bAsyncCollisionCooking;
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
	TArray<FVector> Vertices;
	Vertices.Reserve(static_cast<int32>(Mesh.positions.size()));
	for (const terrain::unreal::UnrealVector3f& Position : Mesh.positions)
	{
		Vertices.Emplace(Position.x, Position.y, Position.z);
	}
	TArray<FVector> Normals;
	Normals.Reserve(static_cast<int32>(Mesh.normals.size()));
	for (const terrain::unreal::UnrealVector3f& Normal : Mesh.normals)
	{
		Normals.Emplace(Normal.x, Normal.y, Normal.z);
	}
	TArray<FColor> Colors;
	Colors.Reserve(static_cast<int32>(Mesh.colors.size()));
	for (const terrain::unreal::VertexColor& Color : Mesh.colors)
	{
		Colors.Emplace(Color.r, Color.g, Color.b, Color.a);
	}
	TArray<int32> Triangles;
	Triangles.Append(Mesh.triangles.data(), static_cast<int32>(Mesh.triangles.size()));

	Component->SetWorldLocation(FVector(Tile.origin.x, Tile.origin.y, Tile.origin.z));
	Component->CreateMeshSection(0, Vertices, Triangles, Normals, TArray<FVector2D>(), Colors,
		TArray<FProcMeshTangent>(), Tile.collision);
	Component->SetCollisionEnabled(
		Tile.collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void UProceduralMeshTerrainBackend::RemoveTile(const terrain::TileKey& Key)
{
	TObjectPtr<UProceduralMeshComponent> Component;
	if (Tiles.RemoveAndCopyValue(TileMapKey(Key), Component) && Component != nullptr)
	{
		Component->DestroyComponent();
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
