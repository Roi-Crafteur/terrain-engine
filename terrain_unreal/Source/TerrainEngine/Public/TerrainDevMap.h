#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Templates/PimplPtr.h"

#include "TerrainDevMap.generated.h"

class ATerrainWorld;
class UInputComponent;
class UTexture2D;
struct FTerrainDevMapState;

// The corner of the screen the minimap stands in.
UENUM(BlueprintType)
enum class ETerrainMapCorner : uint8
{
	TopRight,
	TopLeft,
	BottomRight,
	BottomLeft
};

/**
 * The map of the world of a Terrain World (agent 35, terrain_unreal/README.md, section 16): a
 * minimap in a corner of the screen, centered on the player, and the full-screen dev map (key M):
 * zoom with the wheel from the whole planet down to a few kilometers, drag with the mouse, the
 * layers, what lies under the cursor, a click to go there, the saved places, the coordinates typed.
 *
 * terrain_core renders the images of the map (terrain/map, data only); the Bridge chooses the tiles
 * of each view and renders them on threads of their own (Bridge/map_tile_cache.hpp), never on the
 * game thread; this component only uploads the finished tiles and draws them (Slate). The minimap
 * is meant for the game too: its view, its tiles and its drawing are apart from the dev map, and
 * the discovered zones of a game map will plug in there (Bridge/map_reveal.hpp).
 */
UCLASS(ClassGroup = (Terrain), meta = (DisplayName = "Terrain Dev Map"))
class TERRAINENGINE_API UTerrainDevMapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTerrainDevMapComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// The minimap in a corner of the screen.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map")
	bool bMinimap = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (EditCondition = "bMinimap"))
	ETerrainMapCorner MinimapCorner = ETerrainMapCorner::TopRight;

	// Side of the minimap, screen pixels.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (EditCondition = "bMinimap", ClampMin = "96", ClampMax = "768"))
	float MinimapSize = 256.0f;

	// Meters of the world per pixel of the minimap (Terrain.Map.MinimapZoom, and its keys).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (EditCondition = "bMinimap", ClampMin = "0.25", ClampMax = "4096"))
	float MinimapMetersPerPixel = 4.0f;

	// The minimap turns with the player (its heading up); off: the north up.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (EditCondition = "bMinimap"))
	bool bMinimapRotates = false;

	// The keys zooming the minimap in and out, twice closer or farther each time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map")
	FKey MinimapZoomInKey = EKeys::Add;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map")
	FKey MinimapZoomOutKey = EKeys::Subtract;

	// The key opening and closing the full-screen dev map (also Escape to close it, and the
	// console command Terrain.Map). Never in a Shipping build.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map")
	FKey DevMapKey = EKeys::M;

	// Threads rendering the tiles of the map, besides the game thread and the streaming.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (ClampMin = "1", ClampMax = "4"))
	int32 MapThreads = 1;

	// Tiles kept (each 256 KB on the CPU and on the GPU).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (ClampMin = "32", ClampMax = "1024"))
	int32 KeptTiles = 192;

	// Finished tiles uploaded to their textures per frame.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Map",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 TileUploadsPerFrame = 4;

	// Opens or closes the full-screen dev map (key M). False outside play or in Shipping.
	UFUNCTION(BlueprintCallable, Category = "Terrain|Map")
	bool ToggleDevMap();

	UFUNCTION(BlueprintCallable, Category = "Terrain|Map")
	bool IsDevMapOpen() const;

	UFUNCTION(BlueprintCallable, Category = "Terrain|Map")
	void SetMinimapShown(bool bShown);

	// Meters per pixel of the minimap, within [0.25, 4096].
	UFUNCTION(BlueprintCallable, Category = "Terrain|Map")
	void SetMinimapZoom(float MetersPerPixel);

	UFUNCTION(BlueprintCallable, Category = "Terrain|Map")
	void SetMinimapRotates(bool bRotates);

	// Stops the threads of the map and removes its widgets: before the world it reads goes away
	// (ATerrainWorld::EndPlay). The map starts again by itself once a world plays.
	void StopMap();

	// What the widgets of the dev map ask for (STerrainDevMap): the view of the map, the layers,
	// the places, the typed coordinates, a click.
	void CenterDevMapOnPlayer();
	void ZoomDevMap(const FVector2D& Pixel, float Factor);
	void PanDevMap(const FVector2D& Delta);
	void ClickDevMap(const FVector2D& Pixel);
	void HoverDevMap(const FVector2D& Pixel);
	void ResizeDevMap(const FVector2D& Size);
	void SetLayerShown(const FString& Layer, bool bShown);
	bool IsLayerShown(const FString& Layer) const;
	TArray<FString> LayerNames() const;
	void GoToTyped(const FString& Text);
	void GoToPlace(int32 Index);
	void AddPlaceHere(const FString& Name);
	TArray<FString> PlaceLabels() const;
	FText PlanetText() const;
	FText ReadoutText() const;
	FText MessageText() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ATerrainWorld* TerrainWorld() const;
	bool StartMap();
	void BindKeys();
	void UnbindKeys();
	void PressDevMapKey();
	void ZoomMinimapIn();
	void ZoomMinimapOut();
	void UploadTiles();
	void UpdateMinimap();
	void UpdateDevMap();
	void ShowMinimapWidget(bool bShown);
	void OpenDevMap();
	void CloseDevMap();
	void TickTeleport();
	void TickReadout();
	void RequestTeleport(double X, double Z, double Heading, double Pitch, double Eye,
		bool bEyeAboveGround, const FString& What);
	bool PlayerView(FVector& OutLocation, FRotator& OutRotation) const;
	void SetMessage(const FString& Text);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> TileTextures;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> MapInput;

	// The terrain_core and Bridge side: the source of the map, its tiles and views, the places.
	TPimplPtr<FTerrainDevMapState> State;
};
