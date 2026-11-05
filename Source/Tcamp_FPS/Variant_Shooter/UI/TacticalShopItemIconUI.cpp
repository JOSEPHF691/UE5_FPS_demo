// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/TacticalShopItemIconUI.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Rendering/DrawElements.h"

#include <initializer_list>

void UTacticalShopItemIconUI::SetIconStyle(ETacticalShopItemIconStyle InIconStyle)
{
	if (IconStyle != InIconStyle)
	{
		IconStyle = InIconStyle;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UTacticalShopItemIconUI::SetAccentColor(const FLinearColor& InAccentColor)
{
	if (!AccentColor.Equals(InAccentColor))
	{
		AccentColor = InAccentColor;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UTacticalShopItemIconUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("IconCanvas"));
		Root->SetMinDesiredWidth(128.0f);
		Root->SetMinDesiredHeight(96.0f);
		WidgetTree->RootWidget = Root;
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

int32 UTacticalShopItemIconUI::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	int32 MaxLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const FVector2f WidgetSize(AllottedGeometry.GetLocalSize());
	if (WidgetSize.X < 8.0f || WidgetSize.Y < 8.0f)
	{
		return MaxLayer;
	}

	// Every icon is authored in the same 128 x 96 coordinate system. The whole
	// drawing, including stroke widths, follows the space supplied by its card.
	const FVector2f ArtSize(128.0f, 96.0f);
	const float Scale = FMath::Max(0.01f,
		FMath::Min(WidgetSize.X / ArtSize.X, WidgetSize.Y / ArtSize.Y) * 0.91f);
	const FVector2f Origin = (WidgetSize - ArtSize * Scale) * 0.5f;
	const FLinearColor Primary(
		AccentColor.R, AccentColor.G, AccentColor.B,
		FMath::Clamp(AccentColor.A, 0.0f, 1.0f));
	const FLinearColor Detail(
		AccentColor.R, AccentColor.G, AccentColor.B,
		Primary.A * 0.56f);
	const float MainStroke = FMath::Clamp(2.1f * Scale, 1.0f, 4.0f);
	const float DetailStroke = FMath::Clamp(1.1f * Scale, 0.8f, 2.4f);

	auto DrawPath = [&](std::initializer_list<FVector2f> Path,
		bool bPrimary = true, bool bThick = false)
	{
		if (Path.size() < 2)
		{
			return;
		}
		TArray<FVector2f> Points;
		Points.Reserve(static_cast<int32>(Path.size()));
		for (const FVector2f& Point : Path)
		{
			Points.Add(Origin + Point * Scale);
		}
		FSlateDrawElement::MakeLines(
			OutDrawElements, MaxLayer + 1, AllottedGeometry.ToPaintGeometry(),
			MoveTemp(Points), ESlateDrawEffect::None,
			bPrimary ? Primary : Detail, true,
			bThick ? MainStroke * 1.6f : (bPrimary ? MainStroke : DetailStroke));
	};

	switch (IconStyle)
	{
	case ETacticalShopItemIconStyle::UpgradedPistol:
		// Long slide, compensator, grip and trigger guard distinguish the pistol.
		DrawPath({{25, 37}, {83, 37}, {87, 41}, {101, 41}, {101, 53},
			{57, 53}, {53, 56}, {47, 82}, {31, 79}, {34, 55}, {25, 55},
			{25, 37}}, true, true);
		DrawPath({{26, 45}, {94, 45}, {94, 49}, {27, 49}}, false);
		DrawPath({{60, 54}, {63, 65}, {53, 65}, {50, 58}}, true);
		DrawPath({{36, 58}, {47, 60}, {43, 75}, {34, 73}}, false);
		DrawPath({{50, 31}, {52, 25}, {73, 25}, {75, 31}, {50, 31}}, true);
		DrawPath({{55, 31}, {55, 35}, {70, 35}, {70, 31}}, false);
		DrawPath({{80, 40}, {80, 50}}, false);
		DrawPath({{86, 40}, {86, 50}}, false);
		DrawPath({{92, 41}, {92, 50}}, false);
		DrawPath({{102, 43}, {108, 43}, {108, 51}, {102, 51}}, true);
		break;

	case ETacticalShopItemIconStyle::Rifle:
		// Adjustable stock, receiver, optic, magazine and long handguard.
		DrawPath({{13, 45}, {26, 45}, {33, 48}, {50, 45}, {82, 45},
			{87, 48}, {110, 48}, {110, 58}, {86, 58}, {82, 62},
			{51, 62}, {44, 58}, {32, 58}, {22, 68}, {9, 68},
			{14, 57}, {13, 45}}, true, true);
		DrawPath({{110, 50}, {121, 50}, {121, 55}, {110, 55}}, true);
		DrawPath({{56, 43}, {56, 36}, {67, 36}, {70, 43}}, true);
		DrawPath({{58, 35}, {68, 35}}, false);
		DrawPath({{55, 63}, {52, 79}, {61, 80}, {67, 63}}, true);
		DrawPath({{70, 63}, {75, 77}, {86, 75}, {82, 62}}, true);
		DrawPath({{17, 51}, {31, 51}, {31, 58}}, false);
		DrawPath({{88, 52}, {107, 52}}, false);
		DrawPath({{91, 48}, {91, 58}}, false);
		DrawPath({{97, 48}, {97, 58}}, false);
		DrawPath({{103, 48}, {103, 58}}, false);
		break;

	case ETacticalShopItemIconStyle::Sniper:
		// Oversized scope and slender barrel make this unlike the assault rifle.
		DrawPath({{8, 51}, {31, 51}, {37, 46}, {72, 46}, {78, 50},
			{116, 50}, {116, 56}, {76, 56}, {70, 62}, {40, 62},
			{33, 67}, {13, 67}, {8, 59}, {8, 51}}, true, true);
		DrawPath({{116, 52}, {124, 52}, {124, 54}, {116, 54}}, true);
		DrawPath({{39, 33}, {44, 31}, {80, 31}, {83, 33}, {83, 39},
			{80, 41}, {44, 41}, {39, 39}, {39, 33}}, true);
		DrawPath({{49, 42}, {49, 46}, {72, 46}, {72, 42}}, false);
		DrawPath({{34, 36}, {39, 36}}, false);
		DrawPath({{83, 36}, {88, 36}}, false);
		DrawPath({{48, 62}, {43, 78}, {53, 78}, {59, 62}}, true);
		DrawPath({{64, 62}, {66, 73}, {76, 73}, {78, 57}}, true);
		DrawPath({{100, 56}, {103, 76}}, false);
		DrawPath({{109, 56}, {107, 76}}, false);
		DrawPath({{96, 77}, {111, 77}}, false);
		DrawPath({{14, 55}, {28, 55}, {29, 62}, {17, 62}}, false);
		break;

	case ETacticalShopItemIconStyle::LightArmor:
		// Slim soft vest, narrow shoulder straps and a single chest plate.
		DrawPath({{35, 23}, {48, 18}, {55, 26}, {73, 26}, {80, 18},
			{93, 23}, {98, 43}, {87, 48}, {86, 78}, {42, 78},
			{41, 48}, {30, 43}, {35, 23}}, true, true);
		DrawPath({{48, 18}, {46, 38}, {52, 42}, {76, 42}, {82, 38},
			{80, 18}}, true);
		DrawPath({{52, 45}, {76, 45}, {75, 64}, {53, 64}, {52, 45}}, false);
		DrawPath({{47, 68}, {81, 68}}, false);
		DrawPath({{47, 73}, {81, 73}}, false);
		DrawPath({{42, 50}, {30, 48}}, false);
		DrawPath({{86, 50}, {98, 48}}, false);
		DrawPath({{64, 48}, {64, 62}}, false);
		break;

	case ETacticalShopItemIconStyle::HeavyArmor:
		// Broad shoulder guards, layered chest plates and reinforced waist.
		DrawPath({{27, 22}, {44, 16}, {54, 25}, {74, 25}, {84, 16},
			{101, 22}, {108, 43}, {96, 51}, {92, 83}, {36, 83},
			{32, 51}, {20, 43}, {27, 22}}, true, true);
		DrawPath({{27, 25}, {42, 27}, {47, 43}, {33, 50}, {20, 43}}, true);
		DrawPath({{101, 25}, {86, 27}, {81, 43}, {95, 50}, {108, 43}}, true);
		DrawPath({{49, 31}, {79, 31}, {86, 46}, {78, 68}, {50, 68},
			{42, 46}, {49, 31}}, true);
		DrawPath({{51, 37}, {77, 37}, {79, 49}, {49, 49}, {51, 37}}, false);
		DrawPath({{49, 53}, {79, 53}}, false);
		DrawPath({{64, 54}, {64, 67}}, false);
		DrawPath({{37, 72}, {91, 72}}, true);
		DrawPath({{37, 78}, {91, 78}}, false);
		DrawPath({{31, 32}, {37, 35}}, false);
		DrawPath({{97, 32}, {91, 35}}, false);
		break;
	}

	return MaxLayer + 1;
}
