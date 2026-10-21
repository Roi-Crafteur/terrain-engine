#include "TerrainHorizonMeshes.h"

#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

const FName UTerrainHorizonMeshes::PlanetRadiusParameter(TEXT("PlanetRadiusCm"));

namespace
{
	constexpr int32 SectionCount = 2;

	int32 SectionIndex(terrain::unreal::HorizonSection Section)
	{
		return static_cast<int32>(Section);
	}

	FVector ToFVector(const terrain::unreal::UnrealPoint& Point)
	{
		return FVector(Point.x, Point.y, Point.z);
	}

	void ToUnrealArrays(const terrain::unreal::UnrealMesh& Mesh, TArray<FVector>& Vertices,
		TArray<FVector>& Normals, TArray<FColor>& Colors)
	{
		Vertices.Reserve(static_cast<int32>(Mesh.positions.size()));
		for (const terrain::unreal::UnrealVector3f& Position : Mesh.positions)
		{
			Vertices.Emplace(Position.x, Position.y, Position.z);
		}
		Normals.Reserve(static_cast<int32>(Mesh.normals.size()));
		for (const terrain::unreal::UnrealVector3f& Normal : Mesh.normals)
		{
			Normals.Emplace(Normal.x, Normal.y, Normal.z);
		}
		Colors.Reserve(static_cast<int32>(Mesh.colors.size()));
		for (const terrain::unreal::VertexColor& Color : Mesh.colors)
		{
			Colors.Emplace(Color.r, Color.g, Color.b, Color.a);
		}
	}
} // namespace

void UTerrainHorizonMeshes::Initialize(AActor& Owner, UMaterialInterface* Material,
	double PlanetRadiusCm, bool bCastShadows)
{
	OwnerActor = &Owner;
	bShadows = bCastShadows;
	if (Material != nullptr)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		Instance->SetScalarParameterValue(PlanetRadiusParameter, static_cast<float>(PlanetRadiusCm));
		HorizonMaterial = Instance;
	}
}

void UTerrainHorizonMeshes::Apply(const terrain::unreal::HorizonUpdate& Update,
	terrain::unreal::HorizonTimings& Timings)
{
	for (const terrain::unreal::HorizonUpload& Section : Update.uploads)
	{
		Upload(Section, Timings);
	}
	for (const terrain::unreal::HorizonMove& Move : Update.moves)
	{
		if (Levels.IsValidIndex(Move.level) && Levels[Move.level] != nullptr)
		{
			Levels[Move.level]->SetWorldLocation(ToFVector(Move.origin));
		}
	}
}

void UTerrainHorizonMeshes::Upload(const terrain::unreal::HorizonUpload& Section,
	terrain::unreal::HorizonTimings& Timings)
{
	const double Start = FPlatformTime::Seconds();
	UProceduralMeshComponent* Component = LevelComponent(Section.level);
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FColor> Colors;
	ToUnrealArrays(Section.mesh, Vertices, Normals, Colors);
	const int32 Index = SectionIndex(Section.section);
	FIntPoint& Size = SectionSizes[Section.level * SectionCount + Index];
	Component->SetWorldLocation(ToFVector(Section.origin));
	if (Section.newTriangles)
	{
		TArray<int32> Triangles;
		Triangles.Append(Section.mesh.triangles.data(),
			static_cast<int32>(Section.mesh.triangles.size()));
		Component->CreateMeshSection(Index, Vertices, Triangles, Normals, TArray<FVector2D>(),
			Colors, TArray<FProcMeshTangent>(), false);
		Size = FIntPoint(Vertices.Num(), Triangles.Num() / 3);
	}
	else
	{
		Component->UpdateMeshSection(Index, Vertices, Normals, TArray<FVector2D>(), Colors,
			TArray<FProcMeshTangent>());
		Size.X = Vertices.Num();
	}
	const double UploadMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	Timings.recordUpload(Section.section, Section.conversionMilliseconds, UploadMs,
		Section.mesh.positions.size());
}

UProceduralMeshComponent* UTerrainHorizonMeshes::LevelComponent(int32 Level)
{
	if (!Levels.IsValidIndex(Level))
	{
		Levels.SetNum(Level + 1);
		SectionSizes.SetNumZeroed((Level + 1) * SectionCount);
	}
	if (Levels[Level] != nullptr)
	{
		return Levels[Level];
	}
	UProceduralMeshComponent* Component =
		NewObject<UProceduralMeshComponent>(OwnerActor, NAME_None, RF_Transient);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(bShadows);
	// In world space whatever the transform of the actor, as the tiles.
	Component->SetUsingAbsoluteLocation(true);
	Component->SetUsingAbsoluteRotation(true);
	Component->SetUsingAbsoluteScale(true);
	Component->SetupAttachment(OwnerActor->GetRootComponent());
	Component->RegisterComponent();
	if (HorizonMaterial != nullptr)
	{
		for (int32 Index = 0; Index < SectionCount; ++Index)
		{
			Component->SetMaterial(Index, HorizonMaterial);
		}
	}
	Levels[Level] = Component;
	return Component;
}

void UTerrainHorizonMeshes::RemoveAll()
{
	for (const TObjectPtr<UProceduralMeshComponent>& Component : Levels)
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}
	Levels.Empty();
	SectionSizes.Empty();
}

int64 UTerrainHorizonMeshes::GetVertexCount() const
{
	int64 Vertices = 0;
	for (const FIntPoint& Size : SectionSizes)
	{
		Vertices += Size.X;
	}
	return Vertices;
}

int64 UTerrainHorizonMeshes::GetTriangleCount() const
{
	int64 Triangles = 0;
	for (const FIntPoint& Size : SectionSizes)
	{
		Triangles += Size.Y;
	}
	return Triangles;
}

int64 UTerrainHorizonMeshes::GetMeshBytes() const
{
	return GetVertexCount() * static_cast<int64>(sizeof(FProcMeshVertex)) +
		GetTriangleCount() * 3 * static_cast<int64>(sizeof(uint32));
}
