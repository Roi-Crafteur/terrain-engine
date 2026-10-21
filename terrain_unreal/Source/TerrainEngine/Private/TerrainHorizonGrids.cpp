#include "TerrainHorizonGrids.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	const FName GridSlot(TEXT("Horizon"));

	FVector4f KindColor(terrain::unreal::GridVertexKind Kind)
	{
		const bool bSkirt = Kind == terrain::unreal::GridVertexKind::Skirt;
		const bool bSea = Kind == terrain::unreal::GridVertexKind::Sea;
		return FVector4f(bSkirt ? 1.0f : 0.0f, bSea ? 1.0f : 0.0f, 0.0f, 1.0f);
	}

	void FillDescription(FMeshDescription& Description, const terrain::unreal::GridMesh& Grid)
	{
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		const int32 VertexCount = static_cast<int32>(Grid.vertices.size());
		Description.ReserveNewVertices(VertexCount);
		Description.ReserveNewVertexInstances(VertexCount);
		Description.ReserveNewTriangles(static_cast<int32>(Grid.triangleCount()));
		TArray<FVertexInstanceID> Instances;
		Instances.Reserve(VertexCount);
		for (const terrain::unreal::GridVertex& Vertex : Grid.vertices)
		{
			const FVertexID Id = Description.CreateVertex();
			Positions[Id] = FVector3f(Vertex.i, Vertex.k, 0.0f);
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Id);
			Normals[Instance] = FVector3f(0.0f, 0.0f, 1.0f);
			Tangents[Instance] = FVector3f(1.0f, 0.0f, 0.0f);
			Signs[Instance] = 1.0f;
			Colors[Instance] = KindColor(Vertex.kind);
			UVs[Instance] = FVector2f(Vertex.i, Vertex.k);
			Instances.Add(Instance);
		}
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = GridSlot;
		for (std::size_t Triangle = 0; Triangle < Grid.triangleCount(); ++Triangle)
		{
			const FVertexInstanceID Corners[3] = {
				Instances[Grid.triangles[3 * Triangle]],
				Instances[Grid.triangles[3 * Triangle + 1]],
				Instances[Grid.triangles[3 * Triangle + 2]]};
			Description.CreateTriangle(Group, Corners);
		}
	}
} // namespace

UStaticMesh* BuildHorizonGridMesh(UObject& Outer, const terrain::unreal::GridMesh& Grid, FName Name,
	double UpCm, double DownCm)
{
	FMeshDescription Description;
	FillDescription(Description, Grid);
	UStaticMesh* Mesh = NewObject<UStaticMesh>(&Outer, Name, RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, GridSlot, GridSlot));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bMarkPackageDirty = false;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = false;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = false;
	Mesh->BuildFromMeshDescriptions({&Description}, Params);
	Mesh->SetPositiveBoundsExtension(FVector(0.0, 0.0, UpCm));
	Mesh->SetNegativeBoundsExtension(FVector(0.0, 0.0, DownCm));
	Mesh->CalculateExtendedBounds();
	return Mesh;
}
