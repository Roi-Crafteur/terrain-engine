// The hooks of ATerrainWorld for the tools of the developers (agent 36, terrain_unreal/DEV_TOOLS.md):
// they exist in every build, the tools themselves only with TERRAIN_DEV_TOOLS (never in Shipping,
// TerrainEngine.Build.cs).
#include "TerrainWorld.h"

#include "TerrainDevTools.h"

void ATerrainWorld::StartDevTools()
{
#if TERRAIN_DEV_TOOLS
	if (bDevTools)
	{
		DevTools = NewObject<UTerrainDevTools>(this);
		DevTools->Start(*this);
	}
#endif
}

void ATerrainWorld::StopDevTools()
{
#if TERRAIN_DEV_TOOLS
	if (DevTools != nullptr)
	{
		DevTools->Stop();
		DevTools = nullptr;
	}
#endif
}

void ATerrainWorld::TickDevTools(float DeltaSeconds)
{
#if TERRAIN_DEV_TOOLS
	if (DevTools != nullptr)
	{
		DevTools->Tick(DeltaSeconds);
	}
#else
	(void)DeltaSeconds;
#endif
}

bool ATerrainWorld::TeleportView(double X, double Z, double Heading, double Pitch, double Eye,
	ETerrainEyeMode EyeMode)
{
#if TERRAIN_DEV_TOOLS
	return DevTools != nullptr && DevTools->TeleportView(X, Z, Heading, Pitch, Eye, EyeMode);
#else
	(void)X, (void)Z, (void)Heading, (void)Pitch, (void)Eye, (void)EyeMode;
	return false;
#endif
}

bool ATerrainWorld::ReturnToGamePawn()
{
#if TERRAIN_DEV_TOOLS
	return DevTools != nullptr && DevTools->ReturnToGamePawn(true);
#else
	return false;
#endif
}

UTerrainDevTools* ATerrainWorld::GetDevTools() const
{
	return DevTools;
}
