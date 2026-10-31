#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UTexture2D;

// A tile to draw: its texture (kept alive by the dev map component) and the part of it to show
// (a coarser tile stands in for a finer one not rendered yet), its place in the view before the
// view turns, in pixels of the view.
struct FTerrainMapTileDraw
{
	UTexture2D* Texture = nullptr;
	FBox2f Uv = FBox2f(FVector2f(0.0f, 0.0f), FVector2f(1.0f, 1.0f));
	FVector2D TopLeft = FVector2D::ZeroVector;
	double Side = 0.0;
};

// A saved place on the map: where (pixels of the view, after it turns) and its name.
struct FTerrainMapMarkerDraw
{
	FVector2D Position = FVector2D::ZeroVector;
	FString Label;
	FLinearColor Color = FLinearColor(1.0f, 0.85f, 0.2f);
};

// Everything a map canvas draws in a frame: what the dev map component computed from the Bridge
// (Bridge/map_view.hpp); the canvas only draws it.
struct FTerrainMapDrawList
{
	// The heading shown up (degrees clockwise from the north): the tiles turn by -TurnDegrees
	// about the center of the view.
	double TurnDegrees = 0.0;
	TArray<FTerrainMapTileDraw> Tiles;
	TArray<FTerrainMapMarkerDraw> Markers;
	bool bPlayer = false;
	FVector2D PlayerPosition = FVector2D::ZeroVector;
	double PlayerDegrees = 0.0; // the arrow, clockwise from the top of the screen
	// Where the north lies, clockwise from the top of the screen (a turning minimap), or none.
	TOptional<double> NorthDegrees;
	FString TopText;
	FString BottomText;
};

// The zoom asked at a pixel of the view, by a factor (above 1: closer). An alias: the comma of its
// arguments would split the Slate macro of its argument.
using FTerrainMapZoomHandler = TFunction<void(FVector2D, float)>;

// A map drawn on the screen: the minimap, and the canvas of the full-screen dev map. It draws the
// draw list it is given; the interactive one hands the mouse to the component (zoom, drag, click,
// hover) through its callbacks, in pixels of the view.
class STerrainMapCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(STerrainMapCanvas)
		: _Interactive(false)
		, _DesiredSize(FVector2D(256.0, 256.0))
	{
	}
	SLATE_ARGUMENT(bool, Interactive)
	SLATE_ARGUMENT(FVector2D, DesiredSize)
	SLATE_ARGUMENT(FTerrainMapZoomHandler, OnZoom)
	SLATE_ARGUMENT(TFunction<void(FVector2D)>, OnPan)
	SLATE_ARGUMENT(TFunction<void(FVector2D)>, OnClick)
	SLATE_ARGUMENT(TFunction<void(FVector2D)>, OnHover)
	SLATE_ARGUMENT(TFunction<void(FVector2D)>, OnResize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetDrawList(FTerrainMapDrawList&& List);
	void SetDesiredMapSize(const FVector2D& Size);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
		const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	void PaintTiles(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;
	void PaintOverlay(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;

	bool bInteractive = false;
	FVector2D MapSize = FVector2D(256.0, 256.0);
	FVector2D LastSize = FVector2D::ZeroVector;
	FTerrainMapZoomHandler ZoomHandler;
	TFunction<void(FVector2D)> PanHandler;
	TFunction<void(FVector2D)> ClickHandler;
	TFunction<void(FVector2D)> HoverHandler;
	TFunction<void(FVector2D)> ResizeHandler;

	FTerrainMapDrawList DrawList;
	TArray<FSlateBrush> TileBrushes; // one per tile of the draw list
	FSlateColorBrush Background = FSlateColorBrush(FLinearColor(0.035f, 0.04f, 0.05f, 1.0f));
	FSlateColorBrush White = FSlateColorBrush(FLinearColor::White);

	bool bDragging = false;
	bool bDragMoved = false;
	FVector2D DragStart = FVector2D::ZeroVector;
	FVector2D DragLast = FVector2D::ZeroVector;
};
