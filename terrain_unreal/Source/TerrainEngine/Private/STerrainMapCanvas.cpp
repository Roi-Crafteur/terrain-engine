#include "STerrainMapCanvas.h"

#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Beyond this many pixels a press is a drag, not a click.
	constexpr double ClickSlopPixels = 4.0;
	// Tiles overlap by this much so that no seam shows between them.
	constexpr float TileOverlapPixels = 0.6f;

	FVector2f Rotated(const FVector2f& Point, double Degrees)
	{
		const float Radians = FMath::DegreesToRadians(static_cast<float>(Degrees));
		const float S = FMath::Sin(Radians);
		const float C = FMath::Cos(Radians);
		return FVector2f(Point.X * C - Point.Y * S, Point.X * S + Point.Y * C);
	}

	void ShadowedText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FString& Text, const FVector2f& At, const FLinearColor& Color, int32 Size)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", Size);
		const FVector2f LocalSize = Geometry.GetLocalSize();
		const FVector2f Box(LocalSize.X, static_cast<float>(Size) * 2.0f);
		FSlateDrawElement::MakeText(Out, Layer,
			Geometry.ToPaintGeometry(Box, FSlateLayoutTransform(At + FVector2f(1.0f, 1.0f))), Text,
			Font, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		FSlateDrawElement::MakeText(Out, Layer + 1,
			Geometry.ToPaintGeometry(Box, FSlateLayoutTransform(At)), Text, Font,
			ESlateDrawEffect::None, Color);
	}
} // namespace

void STerrainMapCanvas::Construct(const FArguments& InArgs)
{
	bInteractive = InArgs._Interactive;
	MapSize = InArgs._DesiredSize;
	ZoomHandler = InArgs._OnZoom;
	PanHandler = InArgs._OnPan;
	ClickHandler = InArgs._OnClick;
	HoverHandler = InArgs._OnHover;
	ResizeHandler = InArgs._OnResize;
	SetClipping(EWidgetClipping::ClipToBounds);
}

void STerrainMapCanvas::SetDrawList(FTerrainMapDrawList&& List)
{
	DrawList = MoveTemp(List);
	TileBrushes.SetNum(DrawList.Tiles.Num());
	for (int32 Index = 0; Index < DrawList.Tiles.Num(); ++Index)
	{
		FSlateBrush& Brush = TileBrushes[Index];
		Brush = FSlateBrush();
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Brush.ImageSize = FVector2D(256.0, 256.0);
		Brush.SetResourceObject(DrawList.Tiles[Index].Texture);
		Brush.SetUVRegion(DrawList.Tiles[Index].Uv);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void STerrainMapCanvas::SetDesiredMapSize(const FVector2D& Size)
{
	MapSize = Size;
	Invalidate(EInvalidateWidgetReason::Layout);
}

FVector2D STerrainMapCanvas::ComputeDesiredSize(float) const
{
	return MapSize;
}

int32 STerrainMapCanvas::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
	const FSlateRect&, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle&, bool) const
{
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
		&Background);
	PaintTiles(AllottedGeometry, OutDrawElements, LayerId + 1);
	PaintOverlay(AllottedGeometry, OutDrawElements, LayerId + 2);
	return LayerId + 4;
}

// Each tile at its place, the whole layer turned by -TurnDegrees about the center of the view.
void STerrainMapCanvas::PaintTiles(const FGeometry& Geometry, FSlateWindowElementList& Out,
	int32 Layer) const
{
	const FVector2f LocalSize = Geometry.GetLocalSize();
	const FVector2f Center = LocalSize * 0.5f;
	const FSlateRenderTransform Turn(
		FQuat2f(FMath::DegreesToRadians(static_cast<float>(-DrawList.TurnDegrees))));
	for (int32 Index = 0; Index < DrawList.Tiles.Num(); ++Index)
	{
		const FTerrainMapTileDraw& Tile = DrawList.Tiles[Index];
		const float Side = static_cast<float>(Tile.Side) + TileOverlapPixels;
		const FVector2f Corner(Tile.TopLeft);
		const FVector2f Pivot = (Center - Corner) / Side;
		const FGeometry Placed =
			Geometry.MakeChild(FVector2f(Side, Side), FSlateLayoutTransform(Corner), Turn, Pivot);
		FSlateDrawElement::MakeBox(Out, Layer, Placed.ToPaintGeometry(), &TileBrushes[Index]);
	}
}

// The places, the arrow of the player, the north and the texts.
void STerrainMapCanvas::PaintOverlay(const FGeometry& Geometry, FSlateWindowElementList& Out,
	int32 Layer) const
{
	for (const FTerrainMapMarkerDraw& Marker : DrawList.Markers)
	{
		const FVector2f At(Marker.Position);
		FSlateDrawElement::MakeBox(Out, Layer,
			Geometry.ToPaintGeometry(FVector2f(9.0f, 9.0f),
				FSlateLayoutTransform(At - FVector2f(4.5f, 4.5f))),
			&White, ESlateDrawEffect::None, FLinearColor::Black);
		FSlateDrawElement::MakeBox(Out, Layer,
			Geometry.ToPaintGeometry(FVector2f(7.0f, 7.0f),
				FSlateLayoutTransform(At - FVector2f(3.5f, 3.5f))),
			&White, ESlateDrawEffect::None, Marker.Color);
		ShadowedText(Out, Layer, Geometry, Marker.Label, At + FVector2f(7.0f, -8.0f), Marker.Color, 9);
	}
	if (DrawList.bPlayer)
	{
		const FVector2f At(DrawList.PlayerPosition);
		TArray<FVector2f> Arrow;
		for (const FVector2f& Point : {FVector2f(0.0f, -11.0f), FVector2f(7.0f, 8.0f),
				 FVector2f(0.0f, 3.0f), FVector2f(-7.0f, 8.0f), FVector2f(0.0f, -11.0f)})
		{
			Arrow.Add(At + Rotated(Point, DrawList.PlayerDegrees));
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Arrow,
			ESlateDrawEffect::None, FLinearColor::Black, true, 4.0f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), Arrow,
			ESlateDrawEffect::None, FLinearColor(1.0f, 0.25f, 0.2f), true, 2.0f);
	}
	const FVector2f Size = Geometry.GetLocalSize();
	if (DrawList.NorthDegrees.IsSet())
	{
		const FVector2f North = Size * 0.5f +
			Rotated(FVector2f(0.0f, -0.5f * FMath::Min(Size.X, Size.Y) + 12.0f),
				DrawList.NorthDegrees.GetValue());
		ShadowedText(Out, Layer, Geometry, TEXT("N"), North - FVector2f(4.0f, 8.0f),
			FLinearColor::White, 11);
	}
	ShadowedText(Out, Layer, Geometry, DrawList.TopText, FVector2f(6.0f, 4.0f), FLinearColor::White, 10);
	ShadowedText(Out, Layer, Geometry, DrawList.BottomText, FVector2f(6.0f, Size.Y - 22.0f),
		FLinearColor::White, 10);
}

void STerrainMapCanvas::Tick(const FGeometry& AllottedGeometry, const double, const float)
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size != LastSize && Size.X > 0.0 && Size.Y > 0.0)
	{
		LastSize = Size;
		if (ResizeHandler)
		{
			ResizeHandler(Size);
		}
	}
}

FReply STerrainMapCanvas::OnMouseButtonDown(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bInteractive || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return bInteractive ? FReply::Handled() : FReply::Unhandled();
	}
	bDragging = true;
	bDragMoved = false;
	DragStart = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	DragLast = DragStart;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply STerrainMapCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bInteractive)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (bDragging && HasMouseCapture())
	{
		bDragMoved = bDragMoved || FVector2D::Distance(Local, DragStart) > ClickSlopPixels;
		if (bDragMoved && PanHandler)
		{
			PanHandler(Local - DragLast);
		}
		DragLast = Local;
	}
	if (HoverHandler)
	{
		HoverHandler(Local);
	}
	return FReply::Handled();
}

FReply STerrainMapCanvas::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (!bInteractive || !bDragging || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return bInteractive ? FReply::Handled() : FReply::Unhandled();
	}
	bDragging = false;
	if (!bDragMoved && ClickHandler)
	{
		ClickHandler(DragStart);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply STerrainMapCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bInteractive)
	{
		return FReply::Unhandled();
	}
	if (ZoomHandler)
	{
		// A notch zooms by the square root of 2: two notches, one level of the tiles.
		const float Factor = FMath::Pow(2.0f, 0.5f * MouseEvent.GetWheelDelta());
		ZoomHandler(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), Factor);
	}
	return FReply::Handled();
}
