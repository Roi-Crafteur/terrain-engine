#include "TerrainMapTextures.h"

#include "Engine/Texture2D.h"
#include "RHI.h"
#include "TextureResource.h"

#include "terrain/map/map_frame.hpp"

#include <utility>

namespace TerrainMapTextures
{
	UTexture2D* CreateTileTexture(UObject* Outer, int32 Slot)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(terrain::kMapTilePixels,
			terrain::kMapTilePixels, PF_R8G8B8A8,
			MakeUniqueObjectName(Outer, UTexture2D::StaticClass(),
				FName(*FString::Printf(TEXT("TerrainMapTile%d"), Slot))));
		Texture->Filter = TF_Bilinear;
		Texture->SRGB = true;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		return Texture;
	}

	void UploadTile(UTexture2D& Texture, std::shared_ptr<const terrain::unreal::MapTile> Tile)
	{
		const terrain::MapImage& Image = Tile->image;
		if (Image.width != terrain::kMapTilePixels || Image.height != terrain::kMapTilePixels)
		{
			return;
		}
		// Kept alive until the render thread has copied the bytes.
		auto* Kept = new std::shared_ptr<const terrain::unreal::MapTile>(std::move(Tile));
		auto* Region = new FUpdateTextureRegion2D(0, 0, 0, 0,
			static_cast<uint32>(terrain::kMapTilePixels), static_cast<uint32>(terrain::kMapTilePixels));
		uint8* Bytes = const_cast<uint8*>((*Kept)->image.rgba.data());
		Texture.UpdateTextureRegions(0, 1, Region, static_cast<uint32>(4 * terrain::kMapTilePixels), 4,
			Bytes,
			[Kept](uint8*, const FUpdateTextureRegion2D* Regions)
			{
				delete Kept;
				delete Regions;
			});
	}
} // namespace TerrainMapTextures
