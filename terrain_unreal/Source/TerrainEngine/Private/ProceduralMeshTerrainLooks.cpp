// The view modes of the tools of the developers on the tiles of UProceduralMeshTerrainBackend
// (agent 36, Terrain.View): the colors of the vertices of the ground of each tile by LOD, slope,
// normal or off the heightmap (Bridge/debug_colors.hpp), drawn by the vertex color material of
// Unreal, and the ground hidden for the mode eau. The methods exist in every build, empty without
// TERRAIN_DEV_TOOLS (never in Shipping).
#include "ProceduralMeshTerrainBackend.h"

#include "ProceduralMeshComponent.h"

#if TERRAIN_DEV_TOOLS
#include "Bridge/debug_colors.hpp"
#include "Materials/MaterialInterface.h"
#endif

void UProceduralMeshTerrainBackend::SetDebugLook(terrain::unreal::ViewMode Mode,
	UMaterialInterface* Material, FTerrainOffHeightmap OffHeightmap)
{
#if TERRAIN_DEV_TOOLS
	ViewLook = terrain::unreal::colorsVertices(Mode) && Material != nullptr
		? Mode
		: terrain::unreal::ViewMode::Normal;
	LookMaterial = Material;
	OffHeightmapOf = MoveTemp(OffHeightmap);
	for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& Tile : Tiles)
	{
		if (Tile.Value != nullptr)
		{
			ApplyLook(*Tile.Value, Tile.Key);
		}
	}
	if (ViewLook == terrain::unreal::ViewMode::Normal)
	{
		OwnColors.Empty();
	}
#else
	(void)Mode, (void)Material, (void)OffHeightmap;
#endif
}

void UProceduralMeshTerrainBackend::SetGroundVisible(bool bVisible)
{
	bGroundVisible = bVisible;
	for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& Tile : Tiles)
	{
		if (Tile.Value != nullptr && Tile.Value->GetNumSections() > 0)
		{
			Tile.Value->SetMeshSectionVisible(0, bVisible);
		}
	}
}

// The colors of the mode on the ground of a tile (its own colors kept first), or its own colors
// and material back in the mode normal.
void UProceduralMeshTerrainBackend::ApplyLook(UProceduralMeshComponent& Component,
	const FIntVector& Key)
{
#if TERRAIN_DEV_TOOLS
	FProcMeshSection* Section = Component.GetProcMeshSection(0);
	if (Section == nullptr || Section->ProcVertexBuffer.IsEmpty())
	{
		return;
	}
	if (ViewLook == terrain::unreal::ViewMode::Normal)
	{
		if (const TArray<FColor>* Own = OwnColors.Find(Key))
		{
			Component.UpdateMeshSection(0, TArray<FVector>(), TArray<FVector>(),
				TArray<FVector2D>(), *Own, TArray<FProcMeshTangent>());
		}
		Component.SetMaterial(0, TileMaterial.Get());
		return;
	}
	terrain::unreal::UnrealMesh Mesh;
	TArray<FColor>& Own = OwnColors.FindOrAdd(Key);
	const bool bSaveOwn = Own.IsEmpty();
	for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
	{
		Mesh.positions.push_back({static_cast<float>(Vertex.Position.X),
			static_cast<float>(Vertex.Position.Y), static_cast<float>(Vertex.Position.Z)});
		Mesh.normals.push_back({static_cast<float>(Vertex.Normal.X),
			static_cast<float>(Vertex.Normal.Y), static_cast<float>(Vertex.Normal.Z)});
		Mesh.colors.push_back({Vertex.Color.R, Vertex.Color.G, Vertex.Color.B, Vertex.Color.A});
		if (bSaveOwn)
		{
			Own.Add(Vertex.Color);
		}
	}
	const std::vector<std::uint8_t> Off = ViewLook == terrain::unreal::ViewMode::Rock && OffHeightmapOf
		? OffHeightmapOf(Mesh, Key.Z, Component.GetComponentLocation())
		: std::vector<std::uint8_t>();
	TArray<FColor> Colors;
	Colors.Reserve(static_cast<int32>(Mesh.positions.size()));
	for (const terrain::unreal::VertexColor& Color :
		terrain::unreal::debugColors(ViewLook, Mesh, Key.Z, Off))
	{
		Colors.Emplace(Color.r, Color.g, Color.b, Color.a);
	}
	Component.UpdateMeshSection(0, TArray<FVector>(), TArray<FVector>(), TArray<FVector2D>(),
		Colors, TArray<FProcMeshTangent>());
	Component.SetMaterial(0, LookMaterial.Get());
#else
	(void)Component, (void)Key;
#endif
}
