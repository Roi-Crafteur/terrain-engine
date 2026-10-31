// The view modes of the tools of the developers (agent 36, Terrain.View): normal, filaire (the
// wireframe of Unreal), lod (the tiles by LOD, the levels of the horizon by hue), roche (the
// vertices off the heightmap in orange: 3D rock, caves, edits), eau (the water alone), seche (no
// water, as Terrain.Water 0), normales and pente (the tiles by normal and by slope). The colors come
// from Bridge/debug_colors; the horizon is hidden in the modes that color the tiles otherwise.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/debug_colors.hpp"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "TerrainDevState.h"
#include "TerrainHorizonClipmap.h"
#include "TerrainHorizonMeshes.h"
#include "TerrainMeshBackend.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

namespace
{
	using terrain::unreal::ViewMode;

	// The horizon stays in the modes that show the ground as the tiles do.
	bool HorizonShown(ViewMode Mode)
	{
		return Mode == ViewMode::Normal || Mode == ViewMode::Wireframe || Mode == ViewMode::Lod ||
			Mode == ViewMode::Dry;
	}
} // namespace

void UTerrainDevTools::RunView(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<ViewMode> Mode = terrain::unreal::parseViewMode(Args, Error);
	if (!Mode)
	{
		TerrainDev::Warn(TerrainDev::ToText(Error));
		return;
	}
	SetViewMode(*Mode);
}

void UTerrainDevTools::SetViewMode(ViewMode Mode)
{
	const ViewMode Previous = Dev->Mode;
	Dev->Mode = Mode;
	APlayerController* PlayerController = Controller();
	if (PlayerController != nullptr && (Previous == ViewMode::Wireframe || Mode == ViewMode::Wireframe))
	{
		PlayerController->ConsoleCommand(
			Mode == ViewMode::Wireframe ? TEXT("viewmode wireframe") : TEXT("viewmode lit"));
	}
	if (Previous == ViewMode::Dry || Mode == ViewMode::Dry)
	{
		World->SetWaterShown(Mode != ViewMode::Dry);
	}
	if (UTerrainMeshBackend* Backend = World->Backend.Get())
	{
		UMaterialInterface* VertexColors = nullptr;
		if (GEngine != nullptr)
		{
			UMaterial* DebugColors = ToRawPtr(GEngine->VertexColorMaterial);
			VertexColors = DebugColors;
		}
		FTerrainOffHeightmap OffHeightmap;
		if (Mode == ViewMode::Rock && World->State.IsValid())
		{
			// The world owns the backend: it outlives the function; its frame is read at the call
			// (the origin rebasing moves it).
			ATerrainWorld* Owner = World.Get();
			OffHeightmap = [Owner](const terrain::unreal::UnrealMesh& Mesh, int32 Lod,
							   const FVector& Origin)
			{
				const terrain::unreal::CorePoint Corner = terrain::unreal::toCore(
					Owner->State->Frame, TerrainWorldConversions::ToUnrealPoint(Origin));
				return terrain::unreal::offHeightmap(Mesh, Lod, Corner,
					terrain::unreal::tileLatticeHeights(Owner->State->Session.tiles().heightfield(),
						Lod, Corner.x, Corner.z));
			};
		}
		Backend->SetDebugLook(Mode, VertexColors, MoveTemp(OffHeightmap));
		Backend->SetGroundVisible(Mode != ViewMode::Water);
	}
	if (World->HorizonClipmap != nullptr)
	{
		World->HorizonClipmap->SetHiddenByTools(!HorizonShown(Mode));
		World->HorizonClipmap->SetDebugTint(Mode == ViewMode::Lod || World->bHorizonDebugTint);
	}
	if (World->HorizonMeshes != nullptr)
	{
		World->HorizonMeshes->SetHiddenByTools(!HorizonShown(Mode));
	}
	TerrainDev::Log(FString::Printf(TEXT("view mode %s"),
		*TerrainDev::ToText(std::string(terrain::unreal::viewModeName(Mode)))));
}

// F3 runs "viewmode lit" too in the builds that are not Shipping (the debug keys of Unreal): the
// wireframe is asked again the frame after.
void UTerrainDevTools::TickLooks()
{
	if (!Dev->bReapplyMode)
	{
		return;
	}
	Dev->bReapplyMode = false;
	APlayerController* PlayerController = Controller();
	if (Dev->Mode == ViewMode::Wireframe && PlayerController != nullptr)
	{
		PlayerController->ConsoleCommand(TEXT("viewmode wireframe"));
	}
}

#endif // TERRAIN_DEV_TOOLS
