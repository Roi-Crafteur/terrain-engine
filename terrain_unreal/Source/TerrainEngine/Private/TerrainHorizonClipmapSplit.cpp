// The centers of the cells of the GPU horizon on its finest levels only (agent 40,
// Bridge/horizon_split.hpp: levelHasCenters, coarseSplit, gridsNeeded): the grids with and without
// them, the grid of each level, CoarseSplit on each look. Beyond HorizonCenterLevels (4 by default:
// levels 0 to 3, up to 8 km) the levels keep the grids before agent 40, half the vertices and none
// of the reads of the contour rule.
#include "TerrainHorizonClipmap.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonGrids.h"

namespace
{
	const FName SplitCoarseParameter(TEXT("CoarseSplit"));

#if TERRAIN_DEV_TOOLS
	constexpr bool bSplitEveryGrid = true; // Terrain.HorizonCenters moves the limit while playing
#else
	constexpr bool bSplitEveryGrid = false;
#endif

	// Bytes on the GPU of a grid of these counts (TerrainHorizonGridSlots::VertexBytes, 32-bit
	// indices).
	int64 SplitGridBytes(int64 Vertices, int64 Triangles)
	{
		return Vertices * TerrainHorizonGridSlots::VertexBytes +
			Triangles * 3 * static_cast<int64>(sizeof(int32));
	}

	// The grid of a shape (level 0, the ring, a trim) without the centers of its cells.
	terrain::unreal::GridMesh SplitPlainGrid(const terrain::unreal::HorizonGpuStreamer& Streamer,
		int32 Shape)
	{
		if (Shape == 0)
		{
			return Streamer.level0Grid(false);
		}
		return Shape == 1 ? Streamer.ringGrid(false)
						  : Streamer.trimGrid(Shape - TerrainHorizonGridSlots::FirstTrim, false);
	}
} // namespace

void UTerrainHorizonClipmap::BuildGrids(const terrain::unreal::HorizonGpuStreamer& Streamer)
{
	using TerrainHorizonGridSlots::Shapes;
	const double Start = FPlatformTime::Seconds();
	const int32 Levels = Streamer.params().levels;
	CenterLevels = FMath::Clamp(HorizonLook.CenterLevels, 0, Levels);
	const terrain::unreal::GridSet Needed =
		terrain::unreal::gridsNeeded(CenterLevels, Levels, bSplitEveryGrid);
	Grids.Init(nullptr, 2 * Shapes);
	GridCounts.Init(FIntPoint::ZeroValue, 2 * Shapes);
	int64 Built = 0;
	int64 Every = 0;
	int32 Meshes = 0;
	for (int32 Shape = 0; Shape < Shapes; ++Shape)
	{
		const terrain::unreal::GridMesh Plain = SplitPlainGrid(Streamer, Shape);
		const int64 Centers = static_cast<int64>(terrain::unreal::cellCenterCount(Plain));
		for (int32 Set = 0; Set < 2; ++Set) // with the centers, then without
		{
			const bool bCentered = Set == 0;
			const int32 Index = Set * Shapes + Shape;
			const int64 Vertices =
				static_cast<int64>(Plain.vertices.size()) + (bCentered ? Centers : 0);
			const int64 Triangles =
				static_cast<int64>(Plain.triangleCount()) + (bCentered ? 2 * Centers : 0);
			GridCounts[Index] =
				FIntPoint(static_cast<int32>(Vertices), static_cast<int32>(Triangles));
			Every += SplitGridBytes(Vertices, Triangles);
			const bool bNeeded = Shape == 0
				? (bCentered ? Needed.level0Centered : Needed.level0Plain)
				: (bCentered ? Needed.ringCentered : Needed.ringPlain);
			if (!bNeeded)
			{
				continue;
			}
			Grids[Index] = BuildHorizonGridMesh(*this,
				bCentered ? terrain::unreal::withCellCenters(Plain) : Plain,
				FName(*FString::Printf(TEXT("HorizonGrid%d"), Index)),
				TerrainHorizonGridSlots::UpCm, TerrainHorizonGridSlots::DownCm);
			Built += SplitGridBytes(Vertices, Triangles);
			++Meshes;
		}
	}
	GpuBytes += Built;
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: horizon grids with the centers of their cells on the levels below %d "
			 "(HorizonCenterLevels): %d meshes, %.1f MB on the GPU, built in %.0f ms%s; every grid "
			 "(with and without the centers) %.1f MB"),
		CenterLevels, Meshes, Built / 1.0e6, (FPlatformTime::Seconds() - Start) * 1000.0,
		bSplitEveryGrid ? TEXT(" (every grid: the tools of the developers)") : TEXT(""),
		Every / 1.0e6);
}

UStaticMesh* UTerrainHorizonClipmap::LevelGrid(int32 Level, int32 Shape) const
{
	const int32 Own = terrain::unreal::levelHasCenters(Level, CenterLevels) ? 0 : 1;
	if (UStaticMesh* Mesh = Grids[Own * TerrainHorizonGridSlots::Shapes + Shape].Get())
	{
		return Mesh;
	}
	// Not built (a limit the grids built without the tools do not cover): the other form.
	return Grids[(1 - Own) * TerrainHorizonGridSlots::Shapes + Shape].Get();
}

void UTerrainHorizonClipmap::SetCenterLevels(int32 Levels)
{
	using TerrainHorizonGridSlots::FirstTrim;
	CenterLevels = FMath::Clamp(Levels, 0, Looks.Num());
	for (int32 Level = 0; Level < Looks.Num(); ++Level)
	{
		Looks[Level]->SetScalarParameterValue(SplitCoarseParameter,
			terrain::unreal::coarseSplit(Level, CenterLevels) ? 1.0f : 0.0f);
		if (Bodies.IsValidIndex(Level) && Bodies[Level] != nullptr)
		{
			Bodies[Level]->SetStaticMesh(LevelGrid(Level, Level == 0 ? 0 : 1));
		}
		if (Trims.IsValidIndex(Level) && Trims[Level] != nullptr && TrimShown.IsValidIndex(Level))
		{
			Trims[Level]->SetStaticMesh(LevelGrid(Level, FirstTrim + TrimShown[Level]));
		}
	}
	RecountGrids();
}

// The vertices and triangles drawn: the body of each level and, from level 1 on, its centered
// trim.
void UTerrainHorizonClipmap::RecountGrids()
{
	using TerrainHorizonGridSlots::Shapes;
	Vertices = 0;
	Triangles = 0;
	for (int32 Level = 0; Level < Looks.Num(); ++Level)
	{
		const int32 Own = terrain::unreal::levelHasCenters(Level, CenterLevels) ? 0 : Shapes;
		const FIntPoint Body = GridCounts[Own + (Level == 0 ? 0 : 1)];
		const int32 CenteredTrim =
			TerrainHorizonGridSlots::FirstTrim + TerrainHorizonGridSlots::CenteredTrim;
		const FIntPoint Trim = Level > 0 ? GridCounts[Own + CenteredTrim] : FIntPoint::ZeroValue;
		Vertices += Body.X + Trim.X;
		Triangles += Body.Y + Trim.Y;
	}
}
