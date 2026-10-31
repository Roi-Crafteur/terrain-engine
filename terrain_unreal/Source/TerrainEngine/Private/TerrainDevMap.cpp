#include "TerrainDevMap.h"

#include "Bridge/map_targets.hpp"
#include "Bridge/unreal_frame.hpp"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "STerrainDevMap.h"
#include "STerrainMapCanvas.h"
#include "TerrainDevMapState.h"
#include "TerrainEngineLog.h"
#include "TerrainMapTextures.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "terrain/map/map_style.hpp"

#include <exception>
#include <utility>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	// Coarser tiles drawn in the place of a tile not rendered yet, at most this many levels up.
	constexpr int32 FallbackLevels = 3;
	constexpr int32 MinimapZOrder = 10;
	constexpr float MinimapMargin = 16.0f;

	EHorizontalAlignment HorizontalOf(ETerrainMapCorner Corner)
	{
		return Corner == ETerrainMapCorner::TopLeft || Corner == ETerrainMapCorner::BottomLeft
			? HAlign_Left
			: HAlign_Right;
	}

	EVerticalAlignment VerticalOf(ETerrainMapCorner Corner)
	{
		return Corner == ETerrainMapCorner::TopLeft || Corner == ETerrainMapCorner::TopRight
			? VAlign_Top
			: VAlign_Bottom;
	}

	// The texture of a tile, or of the nearest coarser tile holding it with the part of it to show.
	bool FindTexture(FTerrainDevMapState& Map, const TArray<TObjectPtr<UTexture2D>>& Textures,
		const terrain::MapTileKey& Key, FTerrainMapTileDraw& Out)
	{
		for (int32 Up = 0; Up <= FallbackLevels && Key.level + Up <= terrain::kMapMaxLevel; ++Up)
		{
			const terrain::MapTileKey Coarser{Key.level + Up, Key.tx >> Up, Key.tz >> Up};
			const auto It = Map.Textures.find(Coarser);
			if (It == Map.Textures.end())
			{
				continue;
			}
			It->second.LastFrame = Map.Frame;
			const float Part = 1.0f / static_cast<float>(1 << Up);
			const FVector2f Min(static_cast<float>(Key.tx & ((int64{1} << Up) - 1)) * Part,
				static_cast<float>(Key.tz & ((int64{1} << Up) - 1)) * Part);
			Out.Texture = Textures[It->second.Slot].Get();
			Out.Uv = FBox2f(Min, Min + FVector2f(Part, Part));
			return true;
		}
		return false;
	}

	// The tiles of a view: asked for (Map.Wanted), and drawn from their textures.
	FTerrainMapDrawList DrawTiles(const terrain::unreal::MapView& View, FTerrainDevMapState& Map,
		const TArray<TObjectPtr<UTexture2D>>& Textures)
	{
		FTerrainMapDrawList List;
		List.TurnDegrees = View.heading;
		for (const terrain::unreal::MapTileDraw& Draw : terrain::unreal::tilesInView(View, Map.World))
		{
			Map.Wanted.push_back(Draw.key);
			FTerrainMapTileDraw Tile;
			Tile.TopLeft = FVector2D(Draw.left, Draw.top);
			Tile.Side = Draw.side;
			if (FindTexture(Map, Textures, Draw.key, Tile))
			{
				List.Tiles.Add(Tile);
			}
		}
		return List;
	}
} // namespace

UTerrainDevMapComponent::UTerrainDevMapComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the Terrain World moved its tiles and the player moved: the map shows this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

ATerrainWorld* UTerrainDevMapComponent::TerrainWorld() const
{
	return Cast<ATerrainWorld>(GetOwner());
}

void UTerrainDevMapComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UTerrainDevMapComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopMap();
	UnbindKeys();
	Super::EndPlay(EndPlayReason);
}

bool UTerrainDevMapComponent::StartMap()
{
	ATerrainWorld* Terrain = TerrainWorld();
	if (Terrain == nullptr || !Terrain->State.IsValid())
	{
		return false;
	}
	const terrain::unreal::TerrainSession& Session = Terrain->State->Session;
	State = MakePimpl<FTerrainDevMapState>();
	try
	{
		State->Session = &Session;
		State->World = Session.tiles().world();
		State->Source = std::make_unique<terrain::MapWorldSource>(Session.tiles());
		terrain::unreal::MapCacheParams Params;
		Params.capacity = static_cast<std::size_t>(FMath::Max(32, KeptTiles));
		Params.threads = FMath::Clamp(MapThreads, 1, 4);
		State->Cache = std::make_unique<terrain::unreal::MapTileCache>(
			terrain::unreal::renderTilesOf(*State->Source), terrain::unreal::probesOf(*State->Source),
			terrain::defaultMapLayerNames(), Params);
	}
	catch (const std::exception& Error)
	{
		UE_LOG(LogTerrainEngine, Error, TEXT("Terrain Dev Map: not started: %s"),
			*ToFString(Error.what()));
		State.Reset();
		return false;
	}
	// The places and the teleport of the tools of agent 36 plug in here (terrain/dev/places_file,
	// ATerrainWorld::TeleportView); until then, the places of this play only, and no teleport.
	State->Places = std::make_unique<terrain::unreal::MemoryPlaces>();
	State->Reveal = std::make_unique<terrain::unreal::RevealEverything>();
	State->View.metersPerPixel = 64.0;
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain Dev Map: started (%d thread(s), %d tiles kept)"),
		FMath::Clamp(MapThreads, 1, 4), FMath::Max(32, KeptTiles));
	return true;
}

void UTerrainDevMapComponent::StopMap()
{
	if (!State.IsValid())
	{
		return;
	}
	CloseDevMap();
	ShowMinimapWidget(false);
	if (State->Loading.IsValid() && GetWorld() != nullptr && GetWorld()->GetGameViewport() != nullptr)
	{
		GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(State->Loading.ToSharedRef());
	}
	// The threads of the cache finish their tile and stop before the source they read goes.
	State.Reset();
	TileTextures.Reset();
}

void UTerrainDevMapComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATerrainWorld* Terrain = TerrainWorld();
	const bool bPlaying = Terrain != nullptr && Terrain->State.IsValid();
	if (!bPlaying || (State.IsValid() && State->Session != &Terrain->State->Session))
	{
		StopMap(); // no world, or another one (Terrain.Seed): started again below
	}
	if (!bPlaying || (!State.IsValid() && !StartMap()))
	{
		return;
	}
	BindKeys();
	++State->Frame;
	FVector Location;
	FRotator Rotation;
	State->bHasPlayer = PlayerView(Location, Rotation);
	if (State->bHasPlayer)
	{
		const terrain::unreal::CorePoint Eye =
			terrain::unreal::toCore(Terrain->State->Frame, ToUnrealPoint(Location));
		State->Player = {terrain::wrapCoordinate(Eye.x, State->World.x),
			terrain::wrapCoordinate(Eye.z, State->World.z)};
		State->Heading = terrain::unreal::headingOfYaw(Rotation.Yaw);
		State->Pitch = FRotator::NormalizeAxis(Rotation.Pitch);
	}
	TickTeleport();
	UploadTiles();
	State->Wanted.clear();
	UpdateDevMap();
	UpdateMinimap();
	State->Cache->want(State->Wanted);
	TickReadout();
}

bool UTerrainDevMapComponent::PlayerView(FVector& OutLocation, FRotator& OutRotation) const
{
	const APlayerController* Controller =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return false;
	}
	Controller->GetPlayerViewPoint(OutLocation, OutRotation);
	return true;
}

// The finished tiles to their textures, a few per frame: a free texture, a new one up to
// KeptTiles, or the one drawn the longest ago.
void UTerrainDevMapComponent::UploadTiles()
{
	for (const terrain::MapTileKey& Key : State->Cache->takeReady())
	{
		State->ToUpload.push_back(Key);
	}
	for (int32 Done = 0; Done < TileUploadsPerFrame && !State->ToUpload.empty();)
	{
		const terrain::MapTileKey Key = State->ToUpload.front();
		State->ToUpload.pop_front();
		std::shared_ptr<const terrain::unreal::MapTile> Tile = State->Cache->find(Key);
		if (Tile == nullptr)
		{
			continue;
		}
		int32 Slot = INDEX_NONE;
		if (const auto Held = State->Textures.find(Key); Held != State->Textures.end())
		{
			Slot = Held->second.Slot;
		}
		else if (TileTextures.Num() < FMath::Max(32, KeptTiles))
		{
			Slot = TileTextures.Add(TerrainMapTextures::CreateTileTexture(this, TileTextures.Num()));
		}
		else
		{
			auto Oldest = State->Textures.begin();
			for (auto Entry = State->Textures.begin(); Entry != State->Textures.end(); ++Entry)
			{
				Oldest = Entry->second.LastFrame < Oldest->second.LastFrame ? Entry : Oldest;
			}
			if (Oldest == State->Textures.end() || Oldest->second.LastFrame == State->Frame)
			{
				break; // every texture shows this frame: the rest waits
			}
			Slot = Oldest->second.Slot;
			State->Textures.erase(Oldest);
		}
		State->Textures[Key] = FTerrainMapTileTexture{Slot, State->Frame};
		TerrainMapTextures::UploadTile(*TileTextures[Slot].Get(), std::move(Tile));
		++Done;
	}
}

void UTerrainDevMapComponent::UpdateMinimap()
{
	if (!bMinimap || IsDevMapOpen() || !State->bHasPlayer)
	{
		ShowMinimapWidget(bMinimap && !IsDevMapOpen());
		return;
	}
	ShowMinimapWidget(true);
	if (!State->MinimapCanvas.IsValid())
	{
		return; // no viewport to show it in
	}
	const terrain::unreal::MapView View = terrain::unreal::minimapView(State->Player, State->Heading,
		bMinimapRotates, MinimapMetersPerPixel, MinimapSize, State->World);
	FTerrainMapDrawList List = DrawTiles(View, *State, TileTextures);
	List.bPlayer = true;
	List.PlayerPosition = FVector2D(0.5 * MinimapSize, 0.5 * MinimapSize);
	List.PlayerDegrees = bMinimapRotates ? 0.0 : State->Heading;
	if (bMinimapRotates)
	{
		List.NorthDegrees = -State->Heading;
	}
	List.BottomText = FString::Printf(TEXT("%.2f km"), MinimapSize * MinimapMetersPerPixel / 1000.0);
	if (!State->Source->hasPlanet())
	{
		List.TopText = TEXT("No planet file: no rivers");
	}
	State->MinimapCanvas->SetDesiredMapSize(FVector2D(MinimapSize, MinimapSize));
	State->MinimapCanvas->SetDrawList(MoveTemp(List));
}

void UTerrainDevMapComponent::ShowMinimapWidget(bool bShown)
{
	UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr;
	if (!State.IsValid() || Viewport == nullptr || bShown == State->Minimap.IsValid())
	{
		return;
	}
	if (!bShown)
	{
		Viewport->RemoveViewportWidgetContent(State->Minimap.ToSharedRef());
		State->Minimap.Reset();
		State->MinimapCanvas.Reset();
		return;
	}
	State->Minimap = SNew(SBox)
		.HAlign(HorizontalOf(MinimapCorner))
		.VAlign(VerticalOf(MinimapCorner))
		.Padding(MinimapMargin)
		.Visibility(EVisibility::HitTestInvisible)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
			[
				SAssignNew(State->MinimapCanvas, STerrainMapCanvas)
				.DesiredSize(FVector2D(MinimapSize, MinimapSize))
			]
		];
	Viewport->AddViewportWidgetContent(State->Minimap.ToSharedRef(), MinimapZOrder);
}

void UTerrainDevMapComponent::UpdateDevMap()
{
	if (!IsDevMapOpen())
	{
		return;
	}
	const terrain::unreal::MapView& View = State->View;
	FTerrainMapDrawList List = DrawTiles(View, *State, TileTextures);
	for (const terrain::unreal::MapPlace& Place : State->Places->places())
	{
		const terrain::unreal::ScreenPoint At =
			terrain::unreal::worldToScreen(View, {Place.x, Place.z}, State->World);
		FTerrainMapMarkerDraw Marker;
		Marker.Position = FVector2D(At.x, At.y);
		Marker.Label = ToFString(Place.name);
		if (Place.planet && !State->Source->hasPlanet())
		{
			Marker.Color = FLinearColor(1.0f, 0.35f, 0.3f); // needs the planet file it lacks
		}
		List.Markers.Add(Marker);
	}
	if (State->bHasPlayer)
	{
		const terrain::unreal::ScreenPoint At =
			terrain::unreal::worldToScreen(View, State->Player, State->World);
		List.bPlayer = true;
		List.PlayerPosition = FVector2D(At.x, At.y);
		List.PlayerDegrees = State->Heading - View.heading;
	}
	const terrain::MapSourceKind Kind = State->Source->kindFor(View.metersPerPixel);
	List.TopText = FString::Printf(TEXT("%.2f m per pixel, center x %.0f z %.0f"),
		View.metersPerPixel, View.center.x, View.center.z);
	List.BottomText = Kind == terrain::MapSourceKind::FineRelief
		? TEXT("The fine relief with its rivers, lakes and falls")
		: Kind == terrain::MapSourceKind::Relief ? TEXT("The relief with the water of the planet level")
												 : TEXT("The cells of the planet level (8 km)");
	if (const TSharedPtr<STerrainMapCanvas> Canvas = State->DevMap->GetCanvas())
	{
		Canvas->SetDrawList(MoveTemp(List));
	}
}
