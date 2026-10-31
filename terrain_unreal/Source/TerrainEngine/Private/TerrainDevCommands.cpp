// The console commands of the tools of the developers (agent 36, terrain_unreal/DEV_TOOLS.md), on
// every Terrain World of the world whose tools are on. Registered only with TERRAIN_DEV_TOOLS
// (never in Shipping); their arguments are parsed by Bridge/dev_commands.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"

#include <string>
#include <vector>

namespace
{
	using FDevCommand = void (UTerrainDevTools::*)(const std::vector<std::string>&);

	void Run(UWorld* World, const TArray<FString>& Args, FDevCommand Command)
	{
		if (World == nullptr)
		{
			return;
		}
		std::vector<std::string> Utf8;
		for (const FString& Arg : Args)
		{
			Utf8.push_back(TerrainDev::ToUtf8(Arg));
		}
		bool bAny = false;
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			if (UTerrainDevTools* Tools = It->GetDevTools())
			{
				(Tools->*Command)(Utf8);
				bAny = true;
			}
		}
		if (!bAny)
		{
			TerrainDev::Warn(TEXT("no Terrain World playing with its tools on (bDevTools)"));
		}
	}

	// One console command of the tools: its name, its help, the method it runs.
	struct FDevConsoleCommand
	{
		FDevConsoleCommand(const TCHAR* Name, const TCHAR* Help, FDevCommand Command)
			: Registered(Name, Help,
				  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
					  [Command](const TArray<FString>& Args, UWorld* World)
					  {
						  Run(World, Args, Command);
					  }))
		{
		}

		FAutoConsoleCommandWithWorldAndArgs Registered;
	};

	const FDevConsoleCommand Commands[] = {
		{TEXT("Terrain.Hud"),
			TEXT("The screen of F3: Terrain.Hud [0 off | 1 player | 2 aimed point | 3 performance]; "
				 "without an argument, the next page"),
			&UTerrainDevTools::RunHud},
		{TEXT("Terrain.Goto"),
			TEXT("Terrain.Goto <x> <z> [eye: altitude, or +height above the ground or the water] | "
				 "Terrain.Goto <place>: the view there, the tiles around loaded before the pawn "
				 "moves"),
			&UTerrainDevTools::RunGoto},
		{TEXT("Terrain.Look"),
			TEXT("Terrain.Look <heading: degrees clockwise from the north> <pitch: degrees up>"),
			&UTerrainDevTools::RunLook},
		{TEXT("Terrain.Eye"),
			TEXT("Terrain.Eye <altitude> | Terrain.Eye +<height above the ground or the water>"),
			&UTerrainDevTools::RunEye},
		{TEXT("Terrain.Save"),
			TEXT("Terrain.Save <name> [note...]: the view as a place of the places file"),
			&UTerrainDevTools::RunSave},
		{TEXT("Terrain.Info"),
			TEXT("Terrain.Info [here]: the lines of the point aimed at (here: of the column of the "
				 "eye) in the log"),
			&UTerrainDevTools::RunInfo},
		{TEXT("Terrain.Shot"),
			TEXT("Terrain.Shot <name>: an image of the view once its tiles and horizon are loaded, "
				 "Saved/TerrainEngine/Shots/<name>.png"),
			&UTerrainDevTools::RunShot},
		{TEXT("Terrain.Views"),
			TEXT("Terrain.Views <places, \"*\" or \"prefix*\">...: an image of each, in "
				 "Saved/TerrainEngine/Views/<date>/ with views.txt"),
			&UTerrainDevTools::RunViews},
		{TEXT("Terrain.View"),
			TEXT("Terrain.View normal|filaire|lod|roche|eau|seche|normales|pente: what the tiles "
				 "and the horizon show"),
			&UTerrainDevTools::RunView},
		{TEXT("Terrain.Seed"),
			TEXT("Terrain.Seed <seed> [surface|caves|regions]: the world of another seed without "
				 "leaving play, its planet file read or built aside first"),
			&UTerrainDevTools::RunSeed},
	};

	FAutoConsoleCommandWithWorldAndArgs PawnCommand(TEXT("Terrain.Pawn"),
		TEXT("The pawn of the game back from the flying pawn of the tools, on the ground under the "
			 "view"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* World)
			{
				if (World == nullptr)
				{
					return;
				}
				for (TActorIterator<ATerrainWorld> It(World); It; ++It)
				{
					if (UTerrainDevTools* Tools = It->GetDevTools())
					{
						Tools->RunPawn();
					}
				}
			}));
} // namespace

#endif // TERRAIN_DEV_TOOLS
