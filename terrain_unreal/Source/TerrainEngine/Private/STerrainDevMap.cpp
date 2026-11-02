// The full-screen dev map of agent 35 (STerrainDevMap.h). Only with TERRAIN_DEV_TOOLS: never in
// Shipping.
#include "STerrainDevMap.h"

#if TERRAIN_DEV_TOOLS

#include "GameFramework/InputSettings.h"
#include "STerrainMapCanvas.h"
#include "Styling/CoreStyle.h"
#include "TerrainDevMap.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float PanelWidth = 340.0f;

	TSharedRef<SWidget> Heading(const TCHAR* Text)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(0.75f, 0.85f, 1.0f));
	}

	TSharedRef<SWidget> Note(const TAttribute<FText>& Text, const FLinearColor& Color)
	{
		return SNew(STextBlock)
			.Text(Text)
			.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(Color);
	}
} // namespace

void STerrainDevMap::Construct(const FArguments& InArgs)
{
	Map = InArgs._Map;
	CloseKey = InArgs._CloseKey;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			MakeCanvas()
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		[
			MakePanel()
		]
	];
	RefreshPlaces();
}

TSharedRef<SWidget> STerrainDevMap::MakeCanvas()
{
	const TWeakObjectPtr<UTerrainDevMapComponent> Weak = Map;
	Canvas = SNew(STerrainMapCanvas)
		.Interactive(true)
		.DesiredSize(FVector2D(1280.0, 720.0))
		.OnZoom([Weak](FVector2D Pixel, float Factor)
		{
			if (UTerrainDevMapComponent* Component = Weak.Get())
			{
				Component->ZoomDevMap(Pixel, Factor);
			}
		})
		.OnPan([Weak](FVector2D Delta)
		{
			if (UTerrainDevMapComponent* Component = Weak.Get())
			{
				Component->PanDevMap(Delta);
			}
		})
		.OnClick([Weak](FVector2D Pixel)
		{
			if (UTerrainDevMapComponent* Component = Weak.Get())
			{
				Component->ClickDevMap(Pixel);
			}
		})
		.OnHover([Weak](FVector2D Pixel)
		{
			if (UTerrainDevMapComponent* Component = Weak.Get())
			{
				Component->HoverDevMap(Pixel);
			}
		})
		.OnResize([Weak](FVector2D Size)
		{
			if (UTerrainDevMapComponent* Component = Weak.Get())
			{
				Component->ResizeDevMap(Size);
			}
		});
	return Canvas.ToSharedRef();
}

TSharedRef<SWidget> STerrainDevMap::MakePanel()
{
	return SNew(SBox)
		.WidthOverride(PanelWidth)
		[
			SNew(SBorder)
			.BorderImage(&PanelBrush)
			.Padding(10.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					MakeContent()
				]
			]
		];
}

TSharedRef<SWidget> STerrainDevMap::MakeContent()
{
	const TWeakObjectPtr<UTerrainDevMapComponent> Weak = Map;
	const auto Live = [Weak](FText (UTerrainDevMapComponent::*Get)() const)
	{
		return TAttribute<FText>::CreateLambda([Weak, Get]
		{
			const UTerrainDevMapComponent* Component = Weak.Get();
			return Component != nullptr ? (Component->*Get)() : FText::GetEmpty();
		});
	};
	const FMargin Above(0.0f, 8.0f, 0.0f, 2.0f);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Dev map (M or Escape: close)")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f)
		[
			Note(Live(&UTerrainDevMapComponent::PlanetText), FLinearColor(1.0f, 0.9f, 0.6f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Above)[Heading(TEXT("Layers"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeLayers()]
		+ SVerticalBox::Slot().AutoHeight().Padding(Above)
		[
			Heading(TEXT("Go to x, z (meters of the core)"))
		]
		+ SVerticalBox::Slot().AutoHeight()[MakeGoTo()]
		+ SVerticalBox::Slot().AutoHeight().Padding(Above)
		[
			Heading(TEXT("Add this place (the view of the camera)"))
		]
		+ SVerticalBox::Slot().AutoHeight()[MakeAddPlace()]
		+ SVerticalBox::Slot().AutoHeight().Padding(Above)
		[
			Heading(TEXT("Saved places (click: go there)"))
		]
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(PlacesList, SVerticalBox)]
		+ SVerticalBox::Slot().AutoHeight().Padding(Above)[Heading(TEXT("Under the cursor"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Note(Live(&UTerrainDevMapComponent::ReadoutText), FLinearColor::White)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)
		[
			Note(Live(&UTerrainDevMapComponent::MessageText), FLinearColor(1.0f, 0.8f, 0.3f))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Note(FText::FromString(TEXT("Wheel: zoom. Drag: move. Click: go there, the heading kept, "
										"the eye 1.8 m above the ground or the water. A yellow mark: a "
										"saved place (click it to take its view).")),
				FLinearColor(0.7f, 0.7f, 0.7f))
		];
}

TSharedRef<SWidget> STerrainDevMap::MakeLayers()
{
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const UTerrainDevMapComponent* Component = Map.Get();
	if (Component == nullptr)
	{
		return Box;
	}
	const TWeakObjectPtr<UTerrainDevMapComponent> Weak = Map;
	for (const FString& Layer : Component->LayerNames())
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Weak, Layer]
			{
				const UTerrainDevMapComponent* Self = Weak.Get();
				return Self != nullptr && Self->IsLayerShown(Layer) ? ECheckBoxState::Checked
																	: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Weak, Layer](ECheckBoxState Checked)
			{
				if (UTerrainDevMapComponent* Self = Weak.Get())
				{
					Self->SetLayerShown(Layer, Checked == ECheckBoxState::Checked);
				}
			})
			[
				SNew(STextBlock).Text(FText::FromString(Layer))
			]
		];
	}
	return Box;
}

TSharedRef<SWidget> STerrainDevMap::MakeGoTo()
{
	const TWeakObjectPtr<UTerrainDevMapComponent> Weak = Map;
	const auto Go = [this, Weak]
	{
		if (UTerrainDevMapComponent* Component = Weak.Get())
		{
			Component->GoToTyped(CoordinatesBox->GetText().ToString());
		}
		return FReply::Handled();
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SAssignNew(CoordinatesBox, SEditableTextBox)
			.HintText(FText::FromString(TEXT("-241631, -86277")))
			.OnTextCommitted_Lambda([Go](const FText&, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter)
				{
					Go();
				}
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton).Text(FText::FromString(TEXT("Go"))).OnClicked_Lambda(Go)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Me")))
			.ToolTipText(FText::FromString(TEXT("The map centered on the player")))
			.OnClicked_Lambda([Weak]
			{
				if (UTerrainDevMapComponent* Component = Weak.Get())
				{
					Component->CenterDevMapOnPlayer();
				}
				return FReply::Handled();
			})
		];
}

TSharedRef<SWidget> STerrainDevMap::MakeAddPlace()
{
	const TWeakObjectPtr<UTerrainDevMapComponent> Weak = Map;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SAssignNew(NameBox, SEditableTextBox).HintText(FText::FromString(TEXT("place_name")))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Add this place")))
			.OnClicked_Lambda([this, Weak]
			{
				if (UTerrainDevMapComponent* Component = Weak.Get())
				{
					Component->AddPlaceHere(NameBox->GetText().ToString());
					RefreshPlaces();
				}
				return FReply::Handled();
			})
		];
}

void STerrainDevMap::RefreshPlaces()
{
	const UTerrainDevMapComponent* Component = Map.Get();
	if (!PlacesList.IsValid() || Component == nullptr)
	{
		return;
	}
	PlacesList->ClearChildren();
	const TWeakObjectPtr<UTerrainDevMapComponent> Weak = Map;
	const TArray<FString> Labels = Component->PlaceLabels();
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		PlacesList->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(Labels[Index]))
			.OnClicked_Lambda([Weak, Index]
			{
				if (UTerrainDevMapComponent* Self = Weak.Get())
				{
					Self->GoToPlace(Index);
				}
				return FReply::Handled();
			})
		];
	}
}

bool STerrainDevMap::IsTyping() const
{
	const auto Focused = [](const TSharedPtr<SEditableTextBox>& Box)
	{
		return Box.IsValid() && (Box->HasKeyboardFocus() || Box->HasFocusedDescendants());
	};
	return Focused(CoordinatesBox) || Focused(NameBox);
}

// Escape, or the key of the map when no text is typed, closes it; every other key stays in the map
// (the character and the camera do not move), but those of the console.
FReply STerrainDevMap::OnKeyDown(const FGeometry&, const FKeyEvent& InKeyEvent)
{
	const FKey Pressed = InKeyEvent.GetKey();
	if (Pressed == EKeys::Escape || (Pressed == CloseKey && !IsTyping()))
	{
		if (UTerrainDevMapComponent* Component = Map.Get())
		{
			Component->ToggleDevMap();
		}
		return FReply::Handled();
	}
	const UInputSettings* Settings = GetDefault<UInputSettings>();
	if (Settings != nullptr && Settings->ConsoleKeys.Contains(Pressed))
	{
		return FReply::Unhandled();
	}
	return FReply::Handled();
}

// The mouse stays in the map too: the game never gets its buttons nor its wheel.
FReply STerrainDevMap::OnMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	return FReply::Handled();
}

FReply STerrainDevMap::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	return FReply::Handled();
}

FReply STerrainDevMap::OnMouseWheel(const FGeometry&, const FPointerEvent&)
{
	return FReply::Handled();
}

#endif // TERRAIN_DEV_TOOLS
