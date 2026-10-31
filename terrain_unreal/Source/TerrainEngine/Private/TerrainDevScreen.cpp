// The screen of F3 of the tools of the developers (agent 36): three pages drawn over the game by
// the debug draw service (no HUD to change), refreshed a few times a second: the player (its
// view and the column under it), the point aimed at (the ray of the camera on the tiles that have
// a collision, beyond them on the ground of the core) and the performance. The lines of the
// points come from Bridge/point_info: a module adds its lines there.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/ground_ray.hpp"
#include "Bridge/point_info.hpp"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "CollisionQueryParams.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "TerrainDevState.h"
#include "TerrainHorizonClipmap.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
	using terrain::unreal::HudPage;
	using TerrainDev::ToText;

	constexpr double RefreshSeconds = 0.25;
	constexpr double TraceCm = 50000.0;      // the tiles that have a collision lie within 64 m
	constexpr double MarchMeters = 30000.0;  // beyond, the ground of the core
	constexpr float ScreenLeft = 20.0f;
	constexpr float ScreenTop = 40.0f;
	const FLinearColor Back(0.0f, 0.0f, 0.0f, 0.5f);

	void AddInfo(TArray<FString>& Lines, const std::vector<terrain::unreal::InfoLine>& Info)
	{
		for (const terrain::unreal::InfoLine& Line : Info)
		{
			Lines.Add(FString::Printf(TEXT("  %s: %s"), *ToText(Line.label), *ToText(Line.value)));
		}
	}

	double Megabytes(uint64 Bytes)
	{
		return static_cast<double>(Bytes) / (1024.0 * 1024.0);
	}
} // namespace

void UTerrainDevTools::Draw(UCanvas* Canvas, APlayerController* PlayerController)
{
	if (Canvas == nullptr || !Dev.IsValid() || Dev->Page == HudPage::Off || Dev->bHideScreen ||
		Dev->Lines.IsEmpty() || World == nullptr ||
		(PlayerController != nullptr && PlayerController->GetWorld() != World->GetWorld()))
	{
		return;
	}
	UFont* Font = GEngine != nullptr ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}
	const float LineHeight = Font->GetMaxCharHeight() + 2.0f;
	int32 Width = 0;
	for (const FString& Line : Dev->Lines)
	{
		Width = FMath::Max(Width, Font->GetStringSize(*Line));
	}
	const float Height = LineHeight * static_cast<float>(Dev->Lines.Num());
	FCanvasTileItem Tile(FVector2D(ScreenLeft - 8.0f, ScreenTop - 6.0f),
		FVector2D(static_cast<float>(Width) + 16.0f, Height + 12.0f), Back);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
	float Y = ScreenTop;
	for (const FString& Line : Dev->Lines)
	{
		FCanvasTextItem Text(FVector2D(ScreenLeft, Y), FText::FromString(Line), Font,
			FLinearColor::White);
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
		Y += LineHeight;
	}
}

void UTerrainDevTools::RefreshScreen(bool bNow)
{
	const double Now = FPlatformTime::Seconds();
	if (Dev->Page == HudPage::Off || World == nullptr || !World->State.IsValid())
	{
		Dev->Lines.Reset();
		return;
	}
	if (!bNow && Now - Dev->LinesAt < RefreshSeconds)
	{
		return;
	}
	Dev->LinesAt = Now;
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Terrain F3 - page %d/3 %s - view %s - F3: next page"),
		static_cast<int32>(Dev->Page), *ToText(std::string(terrain::unreal::hudPageName(Dev->Page))),
		*ToText(std::string(terrain::unreal::viewModeName(Dev->Mode)))));
	if (Dev->Page == HudPage::Player)
	{
		ColumnLines(Lines);
	}
	else if (Dev->Page == HudPage::Aimed)
	{
		AimedLines(Lines);
	}
	else
	{
		PerfLines(Lines);
	}
	Dev->ProbeMs = (FPlatformTime::Seconds() - Now) * 1000.0;
	Dev->Lines = MoveTemp(Lines);
}

// The view of the player and the column under its eye.
void UTerrainDevTools::ColumnLines(TArray<FString>& Lines) const
{
	terrain::unreal::ViewPose Pose;
	if (!CurrentPose(Pose))
	{
		Lines.Add(TEXT("no camera"));
		return;
	}
	const terrain::TerrainTiles& Tiles = World->State->Session.tiles();
	const double Surface =
		terrain::unreal::columnTop(Tiles.heightfield().sampleAt(Pose.eye.x, Pose.eye.z));
	Lines.Add(FString::Printf(TEXT("eye: x %.2f, z %.2f m, altitude %.2f m (%+.2f m above the "
		"ground or the water)"), Pose.eye.x, Pose.eye.z, Pose.eye.y, Pose.eye.y - Surface));
	Lines.Add(FString::Printf(TEXT("heading %.1f, pitch %.1f (yaw %.1f); pawn: %s"),
		terrain::unreal::headingOfYaw(Pose.yaw), Pose.pitch, Pose.yaw,
		DevPawn != nullptr ? TEXT("of the tools (flying), Terrain.Pawn to leave it")
						   : TEXT("of the game")));
	const terrain::unreal::InfoQuery Query{Tiles.heightfield(), Tiles.ecology(),
		&World->State->Session.streamer(), Pose.eye.x, Pose.eye.z, std::nullopt};
	AddInfo(Lines, terrain::unreal::describePoint(Query, terrain::unreal::defaultInfoProviders()));
}

// The point aimed at: the ray of the camera on the tiles that have a collision, else on the
// ground of the core.
void UTerrainDevTools::AimedLines(TArray<FString>& Lines) const
{
	const APlayerController* PlayerController = Controller();
	if (PlayerController == nullptr || PlayerController->PlayerCameraManager == nullptr)
	{
		Lines.Add(TEXT("no camera"));
		return;
	}
	const FVector Start = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FVector Forward = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	const terrain::unreal::UnrealFrame& Frame = World->State->Frame;
	const terrain::TerrainTiles& Tiles = World->State->Session.tiles();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainDevAim), true);
	Params.AddIgnoredActor(PlayerController->GetPawn());
	std::optional<terrain::unreal::CorePoint> Point;
	if (World->GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Forward * TraceCm,
			ECC_Visibility, Params))
	{
		Point = terrain::unreal::toCore(Frame, TerrainWorldConversions::ToUnrealPoint(Hit.ImpactPoint));
		Lines.Add(FString::Printf(TEXT("aimed: %.1f m away, on %s"), Hit.Distance / 100.0,
			Hit.GetActor() == World.Get() ? TEXT("a tile (its collision)") : *GetNameSafe(Hit.GetActor())));
	}
	else
	{
		const terrain::Heightfield& Field = Tiles.heightfield();
		const std::optional<terrain::unreal::GroundHit> Ground = terrain::unreal::marchGround(
			terrain::unreal::toCore(Frame, TerrainWorldConversions::ToUnrealPoint(Start)),
			terrain::unreal::coreDirection(Forward.X, Forward.Y, Forward.Z), MarchMeters,
			[&Field](double X, double Z) { return static_cast<double>(Field.heightAt(X, Z)); });
		if (Ground)
		{
			Point = Ground->point;
			Lines.Add(FString::Printf(TEXT("aimed: %.1f m away, on the ground of the core (%d "
				"heights read)"), Ground->distance, Ground->samples));
		}
	}
	if (!Point)
	{
		Lines.Add(TEXT("aimed: nothing within 30 km"));
		return;
	}
	const terrain::unreal::InfoQuery Query{Tiles.heightfield(), Tiles.ecology(),
		&World->State->Session.streamer(), Point->x, Point->z, Point->y};
	AddInfo(Lines, terrain::unreal::describePoint(Query, terrain::unreal::defaultInfoProviders()));
}

void UTerrainDevTools::PerfLines(TArray<FString>& Lines) const
{
	for (const std::string& Line : terrain::unreal::describe(Dev->Perf.summary()))
	{
		Lines.Add(ToText(Line));
	}
	FTerrainWorldState& State = *World->State;
	terrain::unreal::TileStreamer& Streamer = State.Session.streamer();
	Lines.Add(FString::Printf(TEXT("tiles: %d shown, %d pending, %d loaded; %d edits pending"),
		static_cast<int32>(Streamer.shownCount()),
		static_cast<int32>(Streamer.manager().pendingCount()),
		static_cast<int32>(Streamer.manager().loadedCount()),
		static_cast<int32>(State.Session.editsPending())));
	if (State.HorizonGpu != nullptr)
	{
		Lines.Add(FString::Printf(TEXT("horizon (GPU): %s, %.1f MB of samples, %.1f MB on the GPU"),
			State.HorizonGpu->busy() ? TEXT("computing") : TEXT("done"),
			Megabytes(State.HorizonGpu->memoryBytes()),
			World->HorizonClipmap != nullptr
				? Megabytes(static_cast<uint64>(World->HorizonClipmap->GetGpuBytes()))
				: 0.0));
	}
	else if (State.Horizon != nullptr)
	{
		Lines.Add(FString::Printf(TEXT("horizon (CPU): %.1f MB"), Megabytes(State.Horizon->memoryBytes())));
	}
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
	Lines.Add(FString::Printf(TEXT("memory: %.0f MB used, peak %.0f MB; this screen %.2f ms"),
		Megabytes(Memory.UsedPhysical), Megabytes(Memory.PeakUsedPhysical), Dev->ProbeMs));
	Lines.Add(TEXT("water on the GPU: compare with Terrain.View seche; 3D rock: not measured apart"));
	if (Dev->Planet != nullptr)
	{
		Lines.Add(FString::Printf(TEXT("planet of seed %lld: %s, %.0f s"),
			static_cast<long long>(Dev->PendingSeed.seed), *ToText(Dev->Planet->stage()),
			Dev->Planet->seconds()));
	}
}

#endif // TERRAIN_DEV_TOOLS
