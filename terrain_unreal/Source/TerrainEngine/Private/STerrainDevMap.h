#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "InputCoreTypes.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STerrainMapCanvas;
class SVerticalBox;
class UTerrainDevMapComponent;

// The full-screen dev map (agent 35): the map (STerrainMapCanvas) and a panel on its right side:
// the planet file, the layers, the coordinates to go to, the place to add, the saved places, and
// what lies under the cursor. Every action goes to the dev map component; while it is open the
// game gets neither the keys nor the mouse (the character and the camera stay still), Escape or the
// key of the map close it.
class STerrainDevMap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STerrainDevMap)
	{
	}
	SLATE_ARGUMENT(TWeakObjectPtr<UTerrainDevMapComponent>, Map)
	SLATE_ARGUMENT(FKey, CloseKey)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	[[nodiscard]] TSharedPtr<STerrainMapCanvas> GetCanvas() const { return Canvas; }
	// The buttons of the saved places, again (after a place was added).
	void RefreshPlaces();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> MakeCanvas();
	TSharedRef<SWidget> MakePanel();
	TSharedRef<SWidget> MakeContent();
	TSharedRef<SWidget> MakeLayers();
	TSharedRef<SWidget> MakeGoTo();
	TSharedRef<SWidget> MakeAddPlace();
	bool IsTyping() const;

	TWeakObjectPtr<UTerrainDevMapComponent> Map;
	FKey CloseKey;
	TSharedPtr<STerrainMapCanvas> Canvas;
	TSharedPtr<SVerticalBox> PlacesList;
	TSharedPtr<SEditableTextBox> CoordinatesBox;
	TSharedPtr<SEditableTextBox> NameBox;
	FSlateColorBrush PanelBrush = FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
};
