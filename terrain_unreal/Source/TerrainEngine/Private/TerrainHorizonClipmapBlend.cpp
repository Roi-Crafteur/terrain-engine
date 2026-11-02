// The surface blends of the GPU horizon (agent 29): their uploads, and the console command
// Terrain.Blend 0|1 that draws them or not (the scalar parameter SurfaceBlends of the materials of
// the horizon and of the tiles, Scripts/create_horizon_gpu_material.py and
// Scripts/create_temporary_terrain_material.py), to compare their cost and their look.
#include "TerrainHorizonClipmap.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonTextures.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FName SurfaceBlendsParameter(TEXT("SurfaceBlends"));

	// Terrain.Blend 0|1: on every horizon of the world (the tiles bound to level 0 since then; the
	// tiles bound later draw the blends).
	void RunTerrainBlend(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 1)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Blend 0|1"));
			return;
		}
		const bool bShown = FCString::Atoi(*Args[0]) != 0;
		for (TObjectIterator<UTerrainHorizonClipmap> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				It->SetSurfaceBlends(bShown);
			}
		}
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain.Blend %d: the surface blends of the horizon %s"),
			bShown ? 1 : 0, bShown ? TEXT("drawn") : TEXT("left out (the dominant biomes)"));
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainBlendCommand(TEXT("Terrain.Blend"),
		TEXT("Draws (1) or leaves out (0) the surface blends of the far horizon and of level 0 under "
			 "the tiles, to compare their cost and their look: Terrain.Blend 0|1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainBlend));
} // namespace

void UTerrainHorizonClipmap::UploadBlends(std::vector<terrain::unreal::BlendRegion>& Regions)
{
	for (terrain::unreal::BlendRegion& Region : Regions)
	{
		TerrainHorizonTextures::UploadBlend(*BlendTextures[Region.level].Get(), Region);
	}
}

void UTerrainHorizonClipmap::SetSurfaceBlends(bool bShown)
{
	for (TArray<TObjectPtr<UMaterialInstanceDynamic>>* Instances : {&Looks, &SurfaceLooks})
	{
		for (UMaterialInstanceDynamic* Look : *Instances)
		{
			Look->SetScalarParameterValue(SurfaceBlendsParameter, bShown ? 1.0f : 0.0f);
		}
	}
}
