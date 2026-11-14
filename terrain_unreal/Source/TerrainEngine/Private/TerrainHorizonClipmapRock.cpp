// The textures of the rock of the GPU horizon, per level: the bands of its faces (agent 33,
// HorizonBands) and their caps, the cornice at the top of a face (agent 40, HorizonCaps, from the
// regions of Bridge/horizon_gpu_streamer.hpp: capRegions), bound on the look of the level and
// written with the texels of each update. The material reads them under each pixel
// (create_horizon_gpu_material.py: BANDS_HLSL, horizon_ground_hlsl.py: CAPS_HLSL).
#include "TerrainHorizonClipmap.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TerrainHorizonTextures.h"

namespace
{
	const FName RockBandsParameter(TEXT("HorizonBands"));
	const FName RockCapsParameter(TEXT("HorizonCaps"));
} // namespace

void UTerrainHorizonClipmap::CreateRockTextures(int32 Level, UMaterialInstanceDynamic& Look)
{
	UTexture2D* Bands = TerrainHorizonTextures::CreateBands(Side, Level);
	BandTextures.Add(Bands);
	UTexture2D* Caps = TerrainHorizonTextures::CreateCaps(Side, Level);
	CapTextures.Add(Caps);
	Look.SetTextureParameterValue(RockBandsParameter, Bands);
	Look.SetTextureParameterValue(RockCapsParameter, Caps);
}

void UTerrainHorizonClipmap::UploadRock(terrain::unreal::HorizonGpuUpdate& Update)
{
	for (terrain::unreal::TexelRegion& Region : Update.bandRegions)
	{
		TerrainHorizonTextures::UploadBands(*BandTextures[Region.level].Get(), Region);
	}
	for (terrain::unreal::TexelRegion& Region : Update.capRegions)
	{
		TerrainHorizonTextures::UploadCaps(*CapTextures[Region.level].Get(), Region);
	}
}
