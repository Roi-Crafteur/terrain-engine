// The shadows of the GPU horizon by level (agent 40, FTerrainHorizonDrawing::ShadowLevels): its
// finest levels cast them, so that the shadows of the relief go on beyond the tiles, the coarser
// ones never (their cost in the shadow passes, for little: far away the grids already show the
// relief by their normals). The console command Terrain.HorizonShadows of the tools changes it
// while playing (TerrainDevLight.cpp).
#include "TerrainHorizonClipmap.h"

#include "Components/StaticMeshComponent.h"

void UTerrainHorizonClipmap::SetShadowLevels(int32 Levels)
{
	for (int32 Level = 0; Level < Bodies.Num(); ++Level)
	{
		const bool bCast = HorizonLook.bShadows || Level < Levels;
		if (UStaticMeshComponent* Body = Bodies[Level].Get())
		{
			Body->SetCastShadow(bCast);
		}
		if (Trims.IsValidIndex(Level))
		{
			if (UStaticMeshComponent* Trim = Trims[Level].Get())
			{
				Trim->SetCastShadow(bCast);
			}
		}
	}
}
