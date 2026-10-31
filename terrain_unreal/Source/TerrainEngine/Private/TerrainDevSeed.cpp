// Terrain.Seed of the tools of the developers (agent 36): the world of another seed and preset
// without leaving play. Its planet file is prepared first on a thread of its own, the old world
// playing meanwhile (Bridge/planet_job: read when it holds the seed, otherwise built and written
// where the plugin may write, never over a file the user gave); then the world is built again on
// the game thread (the planet file read back: seconds). Only the Terrain World of the play session
// changes, never the one of the level.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Misc/Paths.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

#include <cstdint>
#include <memory>

namespace
{
	using TerrainDev::ToText;
} // namespace

void UTerrainDevTools::RunSeed(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<terrain::unreal::SeedCommand> Command = terrain::unreal::parseSeed(Args, Error);
	if (!Command || Dev->Planet != nullptr)
	{
		TerrainDev::Warn(Command ? FString(TEXT("a planet is already being prepared")) : ToText(Error));
		return;
	}
	const auto NewSeed = static_cast<std::uint64_t>(Command->seed);
	const std::string Base =
		TerrainDev::ToUtf8(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	const terrain::unreal::PlanetChoice Choice =
		terrain::unreal::choosePlanetFile(TerrainDev::ToUtf8(World->PlanetFile), NewSeed, Base);
	Dev->PendingSeed = *Command;
	if (Choice.file.empty())
	{
		SwitchSeed(std::string());
		return;
	}
	Dev->Planet = std::make_unique<terrain::unreal::PlanetJob>(Choice, NewSeed,
		World->State->Session.tiles().world());
	TerrainDev::Log(FString::Printf(TEXT("Terrain.Seed %lld (%s): the planet file %s%s is read, or "
		"built on a thread of its own (minutes for the whole planet), the world playing meanwhile"),
		static_cast<long long>(Command->seed),
		*ToText(std::string(terrain::unreal::presetName(Command->preset))), *ToText(Choice.file),
		Choice.mayWrite ? TEXT("") : TEXT(" (a file of the user: never written)")));
}

void UTerrainDevTools::TickSeed()
{
	if (Dev->Planet == nullptr || !Dev->Planet->done())
	{
		return;
	}
	const terrain::unreal::PlanetJobResult Result = Dev->Planet->take();
	Dev->Planet.reset();
	if (Result.cancelled || !Result.error.empty())
	{
		TerrainDev::Warn(FString::Printf(TEXT("Terrain.Seed %lld: no planet level (%s): the world "
			"stays"), static_cast<long long>(Dev->PendingSeed.seed),
			Result.cancelled ? TEXT("cancelled") : *ToText(Result.error)));
		return;
	}
	TerrainDev::Log(ToText(terrain::unreal::describe(Result.source, Result.file)));
	if (Result.source.built && !Result.source.written)
	{
		TerrainDev::Warn(TEXT("Terrain.Seed: the planet level could not be written: the world stays "
			"(building it again in the game thread would freeze it for minutes)"));
		return;
	}
	SwitchSeed(Result.file);
}

// The Terrain World built again for the seed, the preset and the planet file prepared; the view
// mode of the tools kept.
void UTerrainDevTools::SwitchSeed(const std::string& PlanetFile)
{
	const terrain::unreal::SeedCommand Command = Dev->PendingSeed;
	World->Seed = Command.seed;
	if (Command.preset != terrain::unreal::WorldPreset::Keep)
	{
		World->bCaves = Command.preset != terrain::unreal::WorldPreset::Surface;
		World->bCaveRegions = Command.preset == terrain::unreal::WorldPreset::Regions;
	}
	World->PlanetFile = ToText(PlanetFile);
	const terrain::unreal::ViewMode Mode = Dev->Mode;
	EndHold();
	Dev->Shot.reset();
	Dev->bHideScreen = false;
	World->ReleaseWorld();
	if (!World->StartStreaming())
	{
		TerrainDev::Warn(TEXT("Terrain.Seed: the new world cannot start (see the log above)"));
		World->SetActorTickEnabled(false);
		return;
	}
	Dev->Perf.clear();
	if (Mode != terrain::unreal::ViewMode::Normal)
	{
		Dev->Mode = terrain::unreal::ViewMode::Normal;
		SetViewMode(Mode);
	}
	TerrainDev::Log(FString::Printf(TEXT("Terrain.Seed: the world of seed %lld, preset %s, planet "
		"file %s"), static_cast<long long>(Command.seed),
		!World->bCaves ? TEXT("surface") : World->bCaveRegions ? TEXT("regions") : TEXT("caves"),
		PlanetFile.empty() ? TEXT("none") : *ToText(PlanetFile)));
}

#endif // TERRAIN_DEV_TOOLS
