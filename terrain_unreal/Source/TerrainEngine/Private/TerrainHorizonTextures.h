#pragma once

#include "CoreMinimal.h"

#include "Bridge/horizon_blend_texels.hpp"
#include "Bridge/horizon_normal_texels.hpp"
#include "Bridge/horizon_textures.hpp"

class UTexture2D;

// The textures of the GPU horizon (TerrainHorizonClipmap.h), one of each per level, and their
// uploads: the regions are handed over to the render thread without a copy.
namespace TerrainHorizonTextures
{
	constexpr int32 TexelBytes = terrain::unreal::kTexelFloats * static_cast<int32>(sizeof(float));
	constexpr int32 WaterTexelBytes = static_cast<int32>(sizeof(float));
	constexpr int32 NormalTexelBytes = terrain::unreal::kNormalHalves * 2;
	constexpr int32 BlendTexelBytes =
		terrain::unreal::kBlendChannels * static_cast<int32>(sizeof(uint16));

	// The texels of a level (Bridge/horizon_textures.hpp): float RGBA, nearest, wrapped as the
	// slots.
	UTexture2D* CreateTexels(int32 Side, int32 Level);

	// The detail normals of a level: bilinear, wrapped as the slots, every texel pending until
	// its detail is uploaded (the material keeps the normals of the grid there).
	UTexture2D* CreateNormals(int32 TextureSide, int32 Level);

	// The water of a level (Bridge/horizon_textures.hpp): R32F, nearest, wrapped as the slots, the
	// level of the core over each sample (meters), every texel dry (kNoWaterLevel) until the epochs
	// write it. A material takes a texel below kWetWaterTexel (-1e8) for dry land.
	UTexture2D* CreateWater(int32 Side, int32 Level);

	// The surface blends of a level (Bridge/horizon_blend_texels.hpp, agent 29): RGBA of 16 bits
	// unsigned normalized (PF_A16B16G16R16), nearest, wrapped as the slots, every texel 0 (no
	// blend: the material takes the packed surface of the texel) until the epochs write it.
	UTexture2D* CreateBlend(int32 Side, int32 Level);

	void UploadTexels(UTexture2D& Texture, terrain::unreal::TexelRegion& Region);
	void UploadBlend(UTexture2D& Texture, terrain::unreal::BlendRegion& Region);
	void UploadWater(UTexture2D& Texture, terrain::unreal::WaterRegion& Region);
	void UploadNormals(UTexture2D& Texture, terrain::unreal::NormalRegion& Region);
} // namespace TerrainHorizonTextures
