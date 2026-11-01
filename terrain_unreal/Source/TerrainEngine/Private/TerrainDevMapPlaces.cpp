// Where the dev map sends the player: the coordinates typed, the saved places (the places file of
// the tools of agent 36), the place added from the camera (as Terrain.Save writes it), the view of
// a place (ATerrainWorld::TeleportView), the screen "Loading the terrain" while a click is decided,
// and the texts of the panel: the planet file, what lies under the cursor, the last message
// (UTerrainDevMapComponent). Only with TERRAIN_DEV_TOOLS: never in Shipping.
#include "TerrainDevMap.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/map_places_file.hpp"
#include "Bridge/map_targets.hpp"
#include "Bridge/place_pose.hpp"
#include "Bridge/planet_source.hpp"
#include "Bridge/terrain_session.hpp"
#include "Bridge/unreal_frame.hpp"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "TerrainDevMapState.h"
#include "TerrainDevState.h"
#include "TerrainDevTools.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "terrain/generation/biome.hpp"

#include <optional>
#include <string>
#include <vector>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	constexpr int32 LoadingZOrder = 60;

	const TCHAR* WaterName(terrain::WaterKind Kind)
	{
		switch (Kind)
		{
		case terrain::WaterKind::Ocean:
			return TEXT("sea");
		case terrain::WaterKind::River:
			return TEXT("river");
		case terrain::WaterKind::Lake:
			return TEXT("lake");
		case terrain::WaterKind::SaltLake:
			return TEXT("salt lake");
		case terrain::WaterKind::None:
			break;
		}
		return TEXT("none (dry)");
	}

	TSharedRef<SWidget> LoadingScreen(double X, double Z)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SBorder)
				.Padding(20.0f)
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("Loading the terrain around x %.0f, z %.0f..."), X, Z)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				]
			];
	}
} // namespace

void UTerrainDevMapComponent::SetMessage(const FString& Text)
{
	if (State.IsValid())
	{
		State->Message = Text;
	}
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain Dev Map: %s"), *Text);
}

// The view of a place on the one teleport of the tools (ATerrainWorld::TeleportView, agent 36):
// the map closes, the screen goes black until the world around is shown (agent 38).
void UTerrainDevMapComponent::RequestTeleport(double X, double Z, double Heading, double Pitch,
	double Eye, bool bEyeAboveGround, const FString& What)
{
	ATerrainWorld* Terrain = TerrainWorld();
	if (!State.IsValid() || Terrain == nullptr)
	{
		return;
	}
	State->Click.reset();
	CloseDevMap();
	const bool bAsked = Terrain->TeleportView(X, Z, Heading, Pitch, Eye,
		bEyeAboveGround ? ETerrainEyeMode::Ground : ETerrainEyeMode::Altitude);
	SetMessage(bAsked
			? FString::Printf(TEXT("To %s: x %.0f, z %.0f, heading %.0f, pitch %.0f"), *What, X, Z,
				  Heading, Pitch)
			: FString::Printf(TEXT("The teleport to %s was refused (the tools of the developers are "
								   "off, or the world is not playing: the log says why)"),
				  *What));
}

void UTerrainDevMapComponent::ShowLoading(bool bShown, double X, double Z)
{
	UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr;
	if (!State.IsValid() || Viewport == nullptr)
	{
		return;
	}
	if (State->Loading.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(State->Loading.ToSharedRef());
		State->Loading.Reset();
	}
	if (bShown)
	{
		State->Loading = LoadingScreen(X, Z);
		Viewport->AddViewportWidgetContent(State->Loading.ToSharedRef(), LoadingZOrder);
	}
}

// The places of the places file of the tools (Terrain.PlacesFile, data/dev/places.json by
// default), or of this play only without the tools.
void UTerrainDevMapComponent::RefreshPlaces()
{
	if (!State.IsValid())
	{
		return;
	}
	if (State->Places == nullptr)
	{
		const ATerrainWorld* Terrain = TerrainWorld();
		const UTerrainDevTools* Tools = Terrain != nullptr ? Terrain->GetDevTools() : nullptr;
		if (Tools != nullptr)
		{
			State->Places = std::make_unique<terrain::unreal::PlacesFileSource>(
				TerrainDev::ToPath(Tools->PlacesPath()));
		}
		else
		{
			State->Places = std::make_unique<terrain::unreal::MemoryPlaces>();
		}
	}
	State->PlacesShown = State->Places->places();
	if (!State->Places->error().empty())
	{
		SetMessage(FString::Printf(TEXT("No places: %s"), *ToFString(State->Places->error())));
	}
}

void UTerrainDevMapComponent::GoToTyped(const FString& Text)
{
	const std::optional<terrain::unreal::WorldPoint> Point =
		terrain::unreal::parseCoordinates(TCHAR_TO_UTF8(*Text));
	if (!State.IsValid() || !Point.has_value())
	{
		SetMessage(FString::Printf(TEXT("Not a point: \"%s\" (type x then z, such as "
										"-241631, -86277 or -6 185 104 ; 155 924)"),
			*Text));
		return;
	}
	RequestClick(Point->x, Point->z, State->Heading, State->Pitch, TEXT("the point typed"));
}

void UTerrainDevMapComponent::GoToPlace(int32 Index)
{
	if (!State.IsValid())
	{
		return;
	}
	const std::vector<terrain::dev::Place>& Places = State->PlacesShown;
	if (Index < 0 || Index >= static_cast<int32>(Places.size()))
	{
		return;
	}
	const terrain::dev::Place Place = Places[static_cast<std::size_t>(Index)];
	FString What = ToFString(Place.name);
	if (Place.planet && !State->Source->hasPlanet())
	{
		What += TEXT(" (it needs the planet file, missing: no rivers, lakes nor falls there)");
	}
	RequestTeleport(Place.x, Place.z, Place.heading, Place.pitch, Place.eye,
		Place.eyeMode == terrain::dev::EyeMode::Ground, What);
}

// The view of the camera as a place, as Terrain.Save writes it (Bridge/place_pose of agent 36:
// cameraPose, placeOfView): its point, heading and pitch, its eye at its altitude, rounded to the
// centimeter and to the hundredth of a degree.
void UTerrainDevMapComponent::AddPlaceHere(const FString& Name)
{
	const FString Clean = Name.TrimStartAndEnd();
	ATerrainWorld* Terrain = TerrainWorld();
	FVector Location;
	FRotator Rotation;
	if (!State.IsValid() || Terrain == nullptr || !Terrain->State.IsValid() ||
		!PlayerView(Location, Rotation))
	{
		return;
	}
	if (Clean.IsEmpty())
	{
		SetMessage(TEXT("Type the name of the place first"));
		return;
	}
	const terrain::unreal::CorePoint Eye =
		terrain::unreal::toCore(Terrain->State->Frame, ToUnrealPoint(Location));
	terrain::dev::Place Place;
	try
	{
		Place = terrain::unreal::placeOfView(TerrainDev::ToUtf8(Clean), "added from the dev map",
			terrain::unreal::cameraPose(Eye, Rotation.Pitch, Rotation.Yaw),
			State->Source->hasPlanet(), State->World);
	}
	catch (const terrain::dev::PlacesError& Error)
	{
		SetMessage(FString::Printf(TEXT("Not saved: %s"), *ToFString(Error.what())));
		return;
	}
	if (!State->Places->add(Place))
	{
		SetMessage(FString::Printf(TEXT("Not saved: %s"), *ToFString(State->Places->error())));
		return;
	}
	RefreshPlaces();
	SetMessage(FString::Printf(TEXT("Saved %s: x %.2f, z %.2f, eye %.2f m, heading %.2f, pitch %.2f"),
		*Clean, Place.x, Place.z, Place.eye, Place.heading, Place.pitch));
}

TArray<FString> UTerrainDevMapComponent::PlaceLabels() const
{
	TArray<FString> Labels;
	if (!State.IsValid())
	{
		return Labels;
	}
	for (const terrain::dev::Place& Place : State->PlacesShown)
	{
		FString Label = ToFString(Place.name);
		if (Place.planet)
		{
			Label += TEXT(" [planet]");
		}
		Labels.Add(Label);
	}
	return Labels;
}

void UTerrainDevMapComponent::TickReadout()
{
	if (!IsDevMapOpen())
	{
		return;
	}
	const std::optional<terrain::unreal::WorldPoint>& Hover = State->Hover;
	if (Hover.has_value() && Hover != State->HoverAsked)
	{
		State->Cache->probe(Hover->x, Hover->z);
		State->HoverAsked = Hover;
	}
	if (std::optional<terrain::MapProbe> Probe = State->Cache->takeProbe())
	{
		State->Probe = Probe;
	}
}

FText UTerrainDevMapComponent::ReadoutText() const
{
	if (!State.IsValid() || !State->Probe.has_value())
	{
		return FText::FromString(TEXT("(move the mouse over the map)"));
	}
	const terrain::MapProbe& P = *State->Probe;
	FString Text = FString::Printf(TEXT("x %.0f, z %.0f\nground %.2f m\nwater: %s"), P.x, P.z,
		P.ground, WaterName(P.water));
	if (P.water != terrain::WaterKind::None)
	{
		Text += FString::Printf(TEXT(" at %.2f m (%.2f m deep)"), P.waterLevel, P.waterLevel - P.ground);
	}
	Text += FString::Printf(TEXT("\nbiome: %s\nclimate: %.1f degrees C, humidity %.2f"),
		UTF8_TO_TCHAR(terrain::biomeName(P.biome)), P.temperature, P.humidity);
	if (P.planet)
	{
		Text += FString::Printf(TEXT("\nplanet cell: %.0f mm/yr"), P.precipitation);
		Text += P.discharge > 0.0f ? FString::Printf(TEXT(", river %.0f m3/s"), P.discharge) : FString();
		Text += P.lake >= 0 ? FString::Printf(TEXT(", lake %d"), P.lake) : FString();
	}
	return FText::FromString(Text);
}

FText UTerrainDevMapComponent::PlanetText() const
{
	if (!State.IsValid() || State->Session == nullptr)
	{
		return FText::GetEmpty();
	}
	const terrain::unreal::PlanetSource& Planet = State->Session->planet();
	if (Planet.level == nullptr)
	{
		return FText::FromString(TEXT("NO PLANET FILE: the world has no rivers, lakes nor falls "
									  "(Terrain World: Planet File)"));
	}
	return FText::FromString(Planet.built
			? FString::Printf(TEXT("Planet file BUILT in %.0f s (%s): rivers, lakes and falls"),
				  Planet.seconds, *ToFString(Planet.reason))
			: FString::Printf(TEXT("Planet file read in %.1f s: rivers, lakes and falls"),
				  Planet.seconds));
}

FText UTerrainDevMapComponent::MessageText() const
{
	return State.IsValid() ? FText::FromString(State->Message) : FText::GetEmpty();
}

#endif // TERRAIN_DEV_TOOLS
