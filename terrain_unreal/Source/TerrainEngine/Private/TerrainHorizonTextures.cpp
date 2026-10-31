#include "TerrainHorizonTextures.h"

#include "Engine/Texture2D.h"
#include "RHI.h"
#include "TextureResource.h"

#include <utility>
#include <vector>

namespace TerrainHorizonTextures
{
	namespace
	{
		UTexture2D* CreateTexture(int32 TextureSide, EPixelFormat Format, const TCHAR* Name,
			int32 Level, TextureFilter Filter)
		{
			UTexture2D* Texture = UTexture2D::CreateTransient(TextureSide, TextureSide, Format,
				FName(*FString::Printf(TEXT("%s%d"), Name, Level)));
			Texture->Filter = Filter;
			Texture->SRGB = false;
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Wrap;
			Texture->NeverStream = true;
			Texture->CompressionSettings = TC_HDR;
			return Texture;
		}

		// A region of Values handed over to the render thread without a copy.
		template <class T>
		void Upload(UTexture2D& Texture, int32 X, int32 Z, int32 Width, int32 Height,
			std::vector<T>&& Values, int32 TexelBytesOfTexture)
		{
			auto* Data = new std::vector<T>(std::move(Values));
			auto* Rect = new FUpdateTextureRegion2D(static_cast<uint32>(X), static_cast<uint32>(Z),
				0, 0, static_cast<uint32>(Width), static_cast<uint32>(Height));
			Texture.UpdateTextureRegions(0, 1, Rect,
				static_cast<uint32>(Width * TexelBytesOfTexture),
				static_cast<uint32>(TexelBytesOfTexture), reinterpret_cast<uint8*>(Data->data()),
				[Data](uint8*, const FUpdateTextureRegion2D* Regions)
				{
					delete Data;
					delete Regions;
				});
		}
	} // namespace

	UTexture2D* CreateTexels(int32 Side, int32 Level)
	{
		UTexture2D* Texture =
			CreateTexture(Side, PF_A32B32G32R32F, TEXT("HorizonTexels"), Level, TF_Nearest);
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* CreateNormals(int32 TextureSide, int32 Level)
	{
		UTexture2D* Texture =
			CreateTexture(TextureSide, PF_G16R16F, TEXT("HorizonNormals"), Level, TF_Bilinear);
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		uint16* Texels = static_cast<uint16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		const int64 Count =
			static_cast<int64>(TextureSide) * TextureSide * terrain::unreal::kNormalHalves;
		for (int64 Index = 0; Index < Count; ++Index)
		{
			Texels[Index] = terrain::unreal::kNormalPendingHalf;
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* CreateWater(int32 Side, int32 Level)
	{
		UTexture2D* Texture =
			CreateTexture(Side, PF_R32_FLOAT, TEXT("HorizonWater"), Level, TF_Nearest);
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		float* Texels = static_cast<float*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		const int64 Count = static_cast<int64>(Side) * Side;
		for (int64 Index = 0; Index < Count; ++Index)
		{
			Texels[Index] = terrain::kNoWaterLevel;
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	void UploadTexels(UTexture2D& Texture, terrain::unreal::TexelRegion& Region)
	{
		Upload(Texture, Region.x, Region.z, Region.width, Region.height, std::move(Region.data),
			TexelBytes);
	}

	void UploadWater(UTexture2D& Texture, terrain::unreal::WaterRegion& Region)
	{
		Upload(Texture, Region.x, Region.z, Region.width, Region.height, std::move(Region.data),
			WaterTexelBytes);
	}

	void UploadNormals(UTexture2D& Texture, terrain::unreal::NormalRegion& Region)
	{
		Upload(Texture, Region.x, Region.z, Region.width, Region.height, std::move(Region.data),
			NormalTexelBytes);
	}
} // namespace TerrainHorizonTextures
