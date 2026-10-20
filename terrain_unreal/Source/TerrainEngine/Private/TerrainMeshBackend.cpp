#include "TerrainMeshBackend.h"

#include "ProceduralMeshTerrainBackend.h"
#include "TerrainEngineLog.h"

UTerrainMeshBackend* CreateTerrainMeshBackend(ETerrainMeshType Type, AActor& Owner,
	UMaterialInterface* Material, bool bAsyncCollisionCooking)
{
	switch (Type)
	{
	case ETerrainMeshType::ProceduralMesh:
		break;
	case ETerrainMeshType::DynamicMesh:
		// TODO(phase 10): a UDynamicMeshComponent per tile (GeometryFramework plugin, to add to
		// the .uplugin and the Build.cs): its FDynamicMesh3 filled through EditMesh with the
		// positions, the triangles, a normal overlay and a color overlay; collision with
		// SetComplexAsSimpleCollisionEnabled on the LOD 0 tiles.
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("Terrain World: Dynamic Mesh is not implemented yet, Procedural Mesh "
				 "Component used"));
		break;
	case ETerrainMeshType::RealtimeMesh:
		// TODO(phase 10): a URealtimeMeshComponent per tile with a URealtimeMeshSimple (the
		// RealtimeMeshComponent plugin, not shipped with Unreal), its stream set built from the
		// same arrays.
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("Terrain World: Realtime Mesh Component is not implemented yet, Procedural Mesh "
				 "Component used"));
		break;
	}
	UTerrainMeshBackend* Backend = NewObject<UProceduralMeshTerrainBackend>(&Owner);
	Backend->Initialize(Owner, Material, bAsyncCollisionCooking);
	return Backend;
}
