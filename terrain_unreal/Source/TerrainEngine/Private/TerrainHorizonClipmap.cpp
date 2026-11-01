#include "TerrainHorizonClipmap.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TerrainHorizonGrids.h"
#include "TerrainHorizonTextures.h"

#include <utility>
#include <vector>

namespace
{
	// Bounds of the grids around their plane: the highest ground above, and below the deepest sea,
	// the drop of the curvature at the corner of the coarsest window and its skirt.
	constexpr double GridUpCm = 1.0e6;
	constexpr double GridDownCm = 4.5e6;
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
	// The reveal of the levels (Scripts/horizon_reveal.py, agent 38).
	const FName RevealParameter(TEXT("Reveal"));
	const FName EdgeFadeParameter(TEXT("EdgeFade"));
	const FName EdgeFadeCellsParameter(TEXT("EdgeFadeCells"));
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
	const FName NormalsParameter(TEXT("HorizonNormals"));
	const FName CoarseNormalsParameter(TEXT("CoarseNormals"));
	const FName NormalRatioParameter(TEXT("NormalRatio"));
	const FName HasNormalsParameter(TEXT("HasNormals"));
	// The water of the level and of the next coarser one (task 7b, Bridge/horizon_textures.hpp),
	// and whether the lakes and rivers are drawn (1) or not (0, the ground under them: Terrain.Water).
	const FName WaterParameter(TEXT("HorizonWater"));
	const FName CoarseWaterParameter(TEXT("CoarseWater"));
	const FName InlandWaterParameter(TEXT("InlandWater"));

	// Parameters of the materials of the tiles that read the surface of level 0
	// (Scripts/temporary_terrain_look.py).
	const FName SurfaceTexelsParameter(TEXT("SurfaceTexels"));
	const FName SurfaceCellsParameter(TEXT("SurfaceCells"));
	const FName SurfacePitchParameter(TEXT("SurfacePitchCm"));
	const FName SurfaceFirstXParameter(TEXT("SurfaceFirstX"));
	const FName SurfaceFirstZParameter(TEXT("SurfaceFirstZ"));
	const FName SurfaceOriginParameter(TEXT("SurfaceOrigin"));
	const FName HasSurfaceParameter(TEXT("HasSurface"));
	const FName SurfaceNormalsParameter(TEXT("SurfaceNormals"));
	const FName SurfaceNormalRatioParameter(TEXT("SurfaceNormalRatio"));
	const FName SurfaceHasNormalsParameter(TEXT("SurfaceHasNormals"));
	const FName SurfaceCoveredParameter(TEXT("SurfaceCoveredCm"));
	const FName SurfaceBandParameter(TEXT("SurfaceBandCm"));

	FVector ToFVector(const terrain::unreal::UnrealPoint& Point)
	{
		return FVector(Point.x, Point.y, Point.z);
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
	Reveal.Emplace(Levels, HorizonLook.RevealSeconds);
	NormalRatio = Streamer.normals() != nullptr ? Streamer.normals()->ratio() : 0;
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
		Looks[Level]->SetTextureParameterValue(CoarseWaterParameter,
			WaterTextures[FMath::Min(Level + 1, Levels - 1)]);
		BindNormals(Level);
	}
	GpuBytes += static_cast<int64>(Levels) * Side * Side *
		(TerrainHorizonTextures::TexelBytes + TerrainHorizonTextures::WaterTexelBytes);
	if (Streamer.normals() != nullptr)
	{
		GpuBytes += Streamer.normals()->textureBytes();
	}
	BuildMilliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
}

// The detail normals of a level and of the next coarser one (for the morph), or none.
void UTerrainHorizonClipmap::BindNormals(int32 Level)
{
	UMaterialInstanceDynamic* Look = Looks[Level];
	Look->SetScalarParameterValue(HasNormalsParameter, NormalRatio > 0 ? 1.0f : 0.0f);
	if (NormalRatio == 0)
	{
		return;
	}
	Look->SetScalarParameterValue(NormalRatioParameter, static_cast<float>(NormalRatio));
	Look->SetTextureParameterValue(NormalsParameter, NormalTextures[Level]);
	Look->SetTextureParameterValue(CoarseNormalsParameter,
		NormalTextures[FMath::Min(Level + 1, NormalTextures.Num() - 1)]);
}

void UTerrainHorizonClipmap::CreateLevel(int32 Level,
	const terrain::unreal::HorizonGpuStreamer& Streamer, UMaterialInterface* Material)
{
	UTexture2D* Texture = TerrainHorizonTextures::CreateTexels(Side, Level);
	Textures.Add(Texture);
	UTexture2D* Water = TerrainHorizonTextures::CreateWater(Side, Level);
	WaterTextures.Add(Water);
	if (NormalRatio > 0)
	{
		NormalTextures.Add(TerrainHorizonTextures::CreateNormals(Side * NormalRatio, Level));
	}
	UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Material, this);
	Looks.Add(Look);
	const double PitchCm = terrain::levelPitch(Streamer.params(), Level) * 100.0;
	Look->SetTextureParameterValue(TexelsParameter, Texture);
	Look->SetTextureParameterValue(WaterParameter, Water);
	Look->SetScalarParameterValue(InlandWaterParameter, 1.0f);
	Look->SetScalarParameterValue(CellsParameter,
		static_cast<float>(Streamer.params().cellsPerSide));
	Look->SetScalarParameterValue(HasCoarserParameter,
		Level + 1 < Streamer.params().levels ? 1.0f : 0.0f);
	Look->SetScalarParameterValue(MorphParameter, static_cast<float>(HorizonLook.MorphCells));
	Look->SetScalarParameterValue(EdgeFadeCellsParameter,
		static_cast<float>(HorizonLook.EdgeFadeCells));
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
	if (!Update.regions.empty() || !Update.waterRegions.empty())
	{
		const double Start = FPlatformTime::Seconds();
		std::size_t Texels = 0;
		for (terrain::unreal::TexelRegion& Region : Update.regions)
		{
			Texels += static_cast<std::size_t>(Region.width) *
				static_cast<std::size_t>(Region.height);
			TerrainHorizonTextures::UploadTexels(*Textures[Region.level], Region);
		}
		for (terrain::unreal::WaterRegion& Region : Update.waterRegions)
		{
			TerrainHorizonTextures::UploadWater(*WaterTextures[Region.level], Region);
		}
		Timings.recordUpload(terrain::unreal::HorizonSection::Texels, 0.0,
			(FPlatformTime::Seconds() - Start) * 1000.0, Texels);
	}
	if (!Update.normalRegions.empty() && NormalRatio > 0)
	{
		const double Start = FPlatformTime::Seconds();
		std::size_t Texels = 0;
		for (terrain::unreal::NormalRegion& Region : Update.normalRegions)
		{
			Texels += static_cast<std::size_t>(Region.width) *
				static_cast<std::size_t>(Region.height);
			TerrainHorizonTextures::UploadNormals(*NormalTextures[Region.level], Region);
		}
		Timings.recordUpload(terrain::unreal::HorizonSection::Normals, 0.0,
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
	ApplyReveal();
}

// The reveal of the levels shown goes on (Bridge/horizon_reveal.hpp): their changes to the looks.
void UTerrainHorizonClipmap::ApplyReveal()
{
	const double Now = FPlatformTime::Seconds();
	const double Elapsed = RevealSeconds < 0.0 ? 0.0 : Now - RevealSeconds;
	RevealSeconds = Now;
	for (const int Level : Reveal->advance(Elapsed, bInstantReveal))
	{
		Looks[Level]->SetScalarParameterValue(RevealParameter,
			static_cast<float>(Reveal->reveal(Level)));
		Looks[Level]->SetScalarParameterValue(EdgeFadeParameter,
			static_cast<float>(Reveal->edgeFade(Level)));
	}
}

void UTerrainHorizonClipmap::Place(const terrain::unreal::LevelPlacement& Placement)
{
	const int32 Level = Placement.level;
	Reveal->place(Placement, bInstantReveal);
	if (!Placement.visible)
	{
		// A new place brought level by level: the window of the old place is hidden until its turn.
		Bodies[Level]->SetVisibility(false);
		if (Trims[Level] != nullptr)
		{
			Trims[Level]->SetVisibility(false);
		}
		Placed[Level].Reset();
		return;
	}
	UMaterialInstanceDynamic* Look = Looks[Level];
	// The coarsest level shown has no coarser one to morph toward (the others are hidden).
	Look->SetScalarParameterValue(HasCoarserParameter, Placement.inCoarser.x >= 0 ? 1.0f : 0.0f);
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
	if (Look == nullptr || Textures.IsEmpty())
	{
		return;
	}
	SurfaceLooks.AddUnique(Look);
	Look->SetTextureParameterValue(SurfaceTexelsParameter, Textures[0]);
	Look->SetScalarParameterValue(SurfaceCellsParameter, static_cast<float>(Side - 1));
	Look->SetScalarParameterValue(SurfacePitchParameter, static_cast<float>(Level0PitchCm));
	// The band of the tiles over level 0, where the surface blend of the tiles fades into the
	// samples of the horizon, and the water of the tiles into the water of the horizon: from the
	// edge of the hole to the edge of the area the tiles cover.
	Look->SetScalarParameterValue(SurfaceCoveredParameter,
		static_cast<float>(Fade.coveredMeters * 100.0));
	Look->SetScalarParameterValue(SurfaceBandParameter,
		static_cast<float>(2.0 * Fade.fadeMeters * 100.0));
	Look->SetScalarParameterValue(SurfaceHasNormalsParameter,
		NormalTextures.IsEmpty() ? 0.0f : 1.0f);
	if (!NormalTextures.IsEmpty())
	{
		Look->SetTextureParameterValue(SurfaceNormalsParameter, NormalTextures[0]);
		Look->SetScalarParameterValue(SurfaceNormalRatioParameter, static_cast<float>(NormalRatio));
	}
	if (!Placed.IsEmpty() && Placed[0].IsSet())
	{
		PlaceSurface(*Placed[0]);
	}
}

// The tiles read the texels of level 0 where it stands: its texels are only valid inside its
// window, which holds every tile.
void UTerrainHorizonClipmap::PlaceSurface(const terrain::unreal::LevelPlacement& Placement)
{
	for (UMaterialInstanceDynamic* Look : SurfaceLooks)
	{
		Look->SetScalarParameterValue(SurfaceFirstXParameter,
			static_cast<float>(Placement.firstSlot.x));
		Look->SetScalarParameterValue(SurfaceFirstZParameter,
			static_cast<float>(Placement.firstSlot.z));
		Look->SetDoubleVectorParameterValue(SurfaceOriginParameter,
			FVector4(Placement.origin.x, Placement.origin.y, Placement.origin.z, 0.0));
		Look->SetScalarParameterValue(HasSurfaceParameter, 1.0f);
	}
}

void UTerrainHorizonClipmap::SetWaveOrigin(const terrain::unreal::WaveOrigin& Origin)
{
	for (TArray<TObjectPtr<UMaterialInstanceDynamic>>* Instances : {&Looks, &SurfaceLooks})
	{
		for (UMaterialInstanceDynamic* Look : *Instances)
		{
			Look->SetScalarParameterValue(WaveOriginXParameter, static_cast<float>(Origin.x));
			Look->SetScalarParameterValue(WaveOriginYParameter, static_cast<float>(Origin.y));
		}
	}
}

void UTerrainHorizonClipmap::SetInlandWater(bool bShown)
{
	for (UMaterialInstanceDynamic* Look : Looks)
	{
		Look->SetScalarParameterValue(InlandWaterParameter, bShown ? 1.0f : 0.0f);
	}
}

void UTerrainHorizonClipmap::CopyLook(const UMaterialInterface& Source,
	TConstArrayView<FName> Scalars, TConstArrayView<FName> Vectors)
{
	for (const FName& Name : Scalars)
	{
		float Value = 0.0f;
		if (Source.GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Value))
		{
			for (UMaterialInstanceDynamic* Look : Looks)
			{
				Look->SetScalarParameterValue(Name, Value);
			}
		}
	}
	for (const FName& Name : Vectors)
	{
		FLinearColor Value;
		if (Source.GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Value))
		{
			for (UMaterialInstanceDynamic* Look : Looks)
			{
				Look->SetVectorParameterValue(Name, Value);
			}
		}
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
	for (UMaterialInstanceDynamic* Look : SurfaceLooks)
	{
		Look->SetScalarParameterValue(HasSurfaceParameter, 0.0f);
		Look->SetScalarParameterValue(SurfaceHasNormalsParameter, 0.0f);
	}
	SurfaceLooks.Empty();
}
