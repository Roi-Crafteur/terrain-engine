#include "TerrainHorizonClipmap.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "RHI.h"
#include "TerrainHorizonGrids.h"

#include <utility>
#include <vector>

namespace
{
	// Bounds of the grids around their plane: the highest ground above, and below the deepest sea,
	// the drop of the curvature at the corner of the coarsest window and its skirt.
	constexpr double GridUpCm = 1.0e6;
	constexpr double GridDownCm = 4.5e6;
	constexpr int32 TexelBytes = terrain::unreal::kTexelFloats * static_cast<int32>(sizeof(float));
	constexpr int32 GridVertexBytes = 32; // position, tangents, color and uv on the GPU
	constexpr int32 FirstTrim = 2;        // Grids: level 0, the ring, then the trims
	constexpr int32 CenteredTrim = 4;     // the finer window at the center of the square

	// Parameters of M_TerrainHorizonGpu (Scripts/create_horizon_gpu_material.py).
	const FName TexelsParameter(TEXT("HorizonTexels"));
	const FName CoarseTexelsParameter(TEXT("CoarseTexels"));
	const FName CellsParameter(TEXT("Cells"));
	const FName FirstSlotXParameter(TEXT("FirstSlotX"));
	const FName FirstSlotZParameter(TEXT("FirstSlotZ"));
	const FName CoarseXParameter(TEXT("CoarseX"));
	const FName CoarseZParameter(TEXT("CoarseZ"));
	const FName CoarseFirstXParameter(TEXT("CoarseFirstX"));
	const FName CoarseFirstZParameter(TEXT("CoarseFirstZ"));
	const FName HasCoarserParameter(TEXT("HasCoarser"));
	const FName MorphParameter(TEXT("MorphCells"));
	const FName SkirtParameter(TEXT("SkirtCm"));
	const FName DropParameter(TEXT("DropCm"));
	const FName RadiusParameter(TEXT("PlanetRadiusCm"));
	const FName TintParameter(TEXT("DebugTint"));
	const FName WaveOriginXParameter(TEXT("WaveOriginX"));
	const FName WaveOriginYParameter(TEXT("WaveOriginY"));
	const FName PitchParameter(TEXT("PitchCm"));
	const FName DropCoveredParameter(TEXT("DropCoveredCm"));
	const FName DropFadeParameter(TEXT("DropFadeCm"));

	// Parameters of the materials of the tiles that read the surface of level 0
	// (Scripts/temporary_terrain_look.py).
	const FName SurfaceTexelsParameter(TEXT("SurfaceTexels"));
	const FName SurfaceCellsParameter(TEXT("SurfaceCells"));
	const FName SurfacePitchParameter(TEXT("SurfacePitchCm"));
	const FName SurfaceFirstXParameter(TEXT("SurfaceFirstX"));
	const FName SurfaceFirstZParameter(TEXT("SurfaceFirstZ"));
	const FName SurfaceOriginParameter(TEXT("SurfaceOrigin"));
	const FName HasSurfaceParameter(TEXT("HasSurface"));

	FVector ToFVector(const terrain::unreal::UnrealPoint& Point)
	{
		return FVector(Point.x, Point.y, Point.z);
	}

	UTexture2D* CreateTexels(int32 Side, int32 Level)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Side, Side, PF_A32B32G32R32F,
			FName(*FString::Printf(TEXT("HorizonTexels%d"), Level)));
		Texture->Filter = TF_Nearest;
		Texture->SRGB = false;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->NeverStream = true;
		Texture->CompressionSettings = TC_HDR;
		Texture->UpdateResource();
		return Texture;
	}

	FLinearColor DebugTint(int32 Level)
	{
		FLinearColor Tint =
			FLinearColor::MakeFromHSV8(static_cast<uint8>(Level * 47 % 256), 220, 255);
		Tint.A = 0.6f;
		return Tint;
	}
} // namespace

void UTerrainHorizonClipmap::Initialize(AActor& Owner, UMaterialInterface* Material,
	const terrain::unreal::HorizonGpuStreamer& Streamer, const FTerrainHorizonLook& Look)
{
	const double Start = FPlatformTime::Seconds();
	OwnerActor = &Owner;
	HorizonLook = Look;
	Side = Streamer.side();
	const double Level0Pitch = terrain::levelPitch(Streamer.params(), 0);
	Level0PitchCm = Level0Pitch * 100.0;
	Fade = terrain::unreal::dropFade(Streamer.tileHole(), Level0Pitch);
	const int32 Cells = Streamer.params().cellsPerSide;
	std::vector<terrain::unreal::GridMesh> Shapes{Streamer.level0Grid(), Streamer.ringGrid()};
	for (int32 Variant = 0; Variant < terrain::unreal::kTrimVariants; ++Variant)
	{
		Shapes.push_back(terrain::unreal::trimGrid(Cells, Variant % 3 - 1, Variant / 3 - 1));
	}
	for (std::size_t Index = 0; Index < Shapes.size(); ++Index)
	{
		const terrain::unreal::GridMesh& Shape = Shapes[Index];
		Grids.Add(BuildHorizonGridMesh(*this, Shape,
			FName(*FString::Printf(TEXT("HorizonGrid%d"), static_cast<int32>(Index))), GridUpCm,
			GridDownCm));
		GpuBytes += static_cast<int64>(Shape.vertices.size()) * GridVertexBytes +
			static_cast<int64>(Shape.triangles.size()) * static_cast<int64>(sizeof(int32));
	}
	const int32 Levels = Streamer.params().levels;
	for (int32 Level = 0; Level < Levels; ++Level)
	{
		CreateLevel(Level, Streamer, Material);
		const terrain::unreal::GridMesh& Body = Shapes[Level == 0 ? 0 : 1];
		const terrain::unreal::GridMesh& Trim = Shapes[FirstTrim + CenteredTrim];
		Vertices += static_cast<int64>(Body.vertices.size() +
			(Level > 0 ? Trim.vertices.size() : 0));
		Triangles += static_cast<int64>(Body.triangleCount() +
			(Level > 0 ? Trim.triangleCount() : 0));
	}
	for (int32 Level = 0; Level < Levels; ++Level)
	{
		Looks[Level]->SetTextureParameterValue(CoarseTexelsParameter,
			Textures[FMath::Min(Level + 1, Levels - 1)]);
	}
	GpuBytes += static_cast<int64>(Levels) * Side * Side * TexelBytes;
	BuildMilliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
}

void UTerrainHorizonClipmap::CreateLevel(int32 Level,
	const terrain::unreal::HorizonGpuStreamer& Streamer, UMaterialInterface* Material)
{
	UTexture2D* Texture = CreateTexels(Side, Level);
	Textures.Add(Texture);
	UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Material, this);
	Looks.Add(Look);
	const double PitchCm = terrain::levelPitch(Streamer.params(), Level) * 100.0;
	Look->SetTextureParameterValue(TexelsParameter, Texture);
	Look->SetScalarParameterValue(CellsParameter,
		static_cast<float>(Streamer.params().cellsPerSide));
	Look->SetScalarParameterValue(HasCoarserParameter,
		Level + 1 < Streamer.params().levels ? 1.0f : 0.0f);
	Look->SetScalarParameterValue(MorphParameter, static_cast<float>(HorizonLook.MorphCells));
	Look->SetScalarParameterValue(SkirtParameter,
		static_cast<float>(Streamer.skirtMetersOf(Level) * 100.0));
	Look->SetScalarParameterValue(DropParameter, static_cast<float>(HorizonLook.DropCm));
	Look->SetScalarParameterValue(PitchParameter, static_cast<float>(PitchCm));
	Look->SetScalarParameterValue(DropCoveredParameter, static_cast<float>(Fade.coveredMeters * 100.0));
	Look->SetScalarParameterValue(DropFadeParameter, static_cast<float>(Fade.fadeMeters * 100.0));
	Look->SetScalarParameterValue(RadiusParameter,
		static_cast<float>(Streamer.planetRadiusMeters() * 100.0));
	Look->SetVectorParameterValue(TintParameter,
		HorizonLook.bDebugTint ? DebugTint(Level) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	UStaticMeshComponent* Body = CreateGridComponent(Grids[Level == 0 ? 0 : 1], Look);
	Body->SetWorldScale3D(FVector(PitchCm, PitchCm, 1.0));
	Bodies.Add(Body);
	UStaticMeshComponent* Trim = nullptr;
	if (Level > 0)
	{
		Trim = CreateGridComponent(Grids[FirstTrim + CenteredTrim], Look);
		Trim->SetWorldScale3D(FVector(PitchCm, PitchCm, 1.0));
	}
	Trims.Add(Trim);
	TrimShown.Add(CenteredTrim);
	Placed.AddDefaulted();
}

UStaticMeshComponent* UTerrainHorizonClipmap::CreateGridComponent(UStaticMesh* Mesh,
	UMaterialInstanceDynamic* Look)
{
	UStaticMeshComponent* Component =
		NewObject<UStaticMeshComponent>(OwnerActor, NAME_None, RF_Transient);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(HorizonLook.bShadows);
	Component->bAffectDistanceFieldLighting = false;
	// In world space whatever the transform of the actor, as the tiles; hidden until placed.
	Component->SetUsingAbsoluteLocation(true);
	Component->SetUsingAbsoluteRotation(true);
	Component->SetUsingAbsoluteScale(true);
	Component->SetVisibility(false);
	Component->SetupAttachment(OwnerActor->GetRootComponent());
	Component->RegisterComponent();
	Component->SetMaterial(0, Look);
	return Component;
}

void UTerrainHorizonClipmap::Apply(terrain::unreal::HorizonGpuUpdate& Update,
	terrain::unreal::HorizonTimings& Timings)
{
	if (!Update.regions.empty())
	{
		const double Start = FPlatformTime::Seconds();
		std::size_t Texels = 0;
		for (terrain::unreal::TexelRegion& Region : Update.regions)
		{
			Texels += static_cast<std::size_t>(Region.width) *
				static_cast<std::size_t>(Region.height);
			UploadRegion(Region);
		}
		Timings.recordUpload(terrain::unreal::HorizonSection::Texels, 0.0,
			(FPlatformTime::Seconds() - Start) * 1000.0, Texels);
	}
	TArray<bool> Changed;
	Changed.Init(false, Bodies.Num());
	for (const terrain::unreal::LevelPlacement& Placement : Update.placements)
	{
		Place(Placement);
		Changed[Placement.level] = true;
	}
	// The texels of the coarser level each finer one reads for the morph of its normals.
	for (int32 Level = 0; Level + 1 < Bodies.Num(); ++Level)
	{
		if ((Changed[Level] || Changed[Level + 1]) && Placed[Level + 1].IsSet())
		{
			const terrain::HorizonIndex& First = Placed[Level + 1]->firstSlot;
			UMaterialInstanceDynamic* Look = Looks[Level];
			Look->SetScalarParameterValue(CoarseFirstXParameter, static_cast<float>(First.x));
			Look->SetScalarParameterValue(CoarseFirstZParameter, static_cast<float>(First.z));
		}
	}
}

// The texels of a region, handed over to the render thread without a copy.
void UTerrainHorizonClipmap::UploadRegion(terrain::unreal::TexelRegion& Region)
{
	auto* Texels = new std::vector<float>(std::move(Region.data));
	auto* Rect = new FUpdateTextureRegion2D(static_cast<uint32>(Region.x),
		static_cast<uint32>(Region.z), 0, 0, static_cast<uint32>(Region.width),
		static_cast<uint32>(Region.height));
	Textures[Region.level]->UpdateTextureRegions(0, 1, Rect,
		static_cast<uint32>(Region.width * TexelBytes), static_cast<uint32>(TexelBytes),
		reinterpret_cast<uint8*>(Texels->data()),
		[Texels](uint8*, const FUpdateTextureRegion2D* Regions)
		{
			delete Texels;
			delete Regions;
		});
}

void UTerrainHorizonClipmap::Place(const terrain::unreal::LevelPlacement& Placement)
{
	const int32 Level = Placement.level;
	UMaterialInstanceDynamic* Look = Looks[Level];
	Look->SetScalarParameterValue(FirstSlotXParameter, static_cast<float>(Placement.firstSlot.x));
	Look->SetScalarParameterValue(FirstSlotZParameter, static_cast<float>(Placement.firstSlot.z));
	// The coarsest level has no coarser one (-1): unused there (HasCoarser).
	Look->SetScalarParameterValue(CoarseXParameter,
		static_cast<float>(FMath::Max<int64>(0, Placement.inCoarser.x)));
	Look->SetScalarParameterValue(CoarseZParameter,
		static_cast<float>(FMath::Max<int64>(0, Placement.inCoarser.z)));
	const FVector Origin = ToFVector(Placement.origin);
	Bodies[Level]->SetWorldLocation(Origin);
	Bodies[Level]->SetVisibility(true);
	if (UStaticMeshComponent* Trim = Trims[Level])
	{
		if (TrimShown[Level] != Placement.trim)
		{
			Trim->SetStaticMesh(Grids[FirstTrim + Placement.trim]);
			TrimShown[Level] = Placement.trim;
		}
		Trim->SetWorldLocation(Origin);
		Trim->SetVisibility(true);
	}
	Placed[Level] = Placement;
	if (Level == 0)
	{
		PlaceSurface(Placement);
	}
}

void UTerrainHorizonClipmap::BindSurfaceLook(UMaterialInstanceDynamic* Look)
{
	SurfaceLook = Look;
	if (SurfaceLook == nullptr || Textures.IsEmpty())
	{
		return;
	}
	SurfaceLook->SetTextureParameterValue(SurfaceTexelsParameter, Textures[0]);
	SurfaceLook->SetScalarParameterValue(SurfaceCellsParameter, static_cast<float>(Side - 1));
	SurfaceLook->SetScalarParameterValue(SurfacePitchParameter, static_cast<float>(Level0PitchCm));
	if (!Placed.IsEmpty() && Placed[0].IsSet())
	{
		PlaceSurface(*Placed[0]);
	}
}

// The tiles read the texels of level 0 where it stands: its texels are only valid inside its
// window, which holds every tile.
void UTerrainHorizonClipmap::PlaceSurface(const terrain::unreal::LevelPlacement& Placement)
{
	if (SurfaceLook == nullptr)
	{
		return;
	}
	SurfaceLook->SetScalarParameterValue(SurfaceFirstXParameter,
		static_cast<float>(Placement.firstSlot.x));
	SurfaceLook->SetScalarParameterValue(SurfaceFirstZParameter,
		static_cast<float>(Placement.firstSlot.z));
	SurfaceLook->SetDoubleVectorParameterValue(SurfaceOriginParameter,
		FVector4(Placement.origin.x, Placement.origin.y, Placement.origin.z, 0.0));
	SurfaceLook->SetScalarParameterValue(HasSurfaceParameter, 1.0f);
}

void UTerrainHorizonClipmap::SetWaveOrigin(const terrain::unreal::WaveOrigin& Origin)
{
	for (UMaterialInstanceDynamic* Look : Looks)
	{
		Look->SetScalarParameterValue(WaveOriginXParameter, static_cast<float>(Origin.x));
		Look->SetScalarParameterValue(WaveOriginYParameter, static_cast<float>(Origin.y));
	}
	if (SurfaceLook != nullptr)
	{
		SurfaceLook->SetScalarParameterValue(WaveOriginXParameter, static_cast<float>(Origin.x));
		SurfaceLook->SetScalarParameterValue(WaveOriginYParameter, static_cast<float>(Origin.y));
	}
}

void UTerrainHorizonClipmap::RemoveAll()
{
	for (TArray<TObjectPtr<UStaticMeshComponent>>* Components : {&Bodies, &Trims})
	{
		for (const TObjectPtr<UStaticMeshComponent>& Component : *Components)
		{
			if (Component != nullptr)
			{
				Component->DestroyComponent();
			}
		}
		Components->Empty();
	}
	Placed.Empty();
	TrimShown.Empty();
	if (SurfaceLook != nullptr)
	{
		SurfaceLook->SetScalarParameterValue(HasSurfaceParameter, 0.0f);
		SurfaceLook = nullptr;
	}
}
