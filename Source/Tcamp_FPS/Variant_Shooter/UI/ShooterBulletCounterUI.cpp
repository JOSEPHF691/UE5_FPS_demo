// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/Widget.h"

namespace ShooterHudWidgetNames
{
	static const FName DamageOverlay(TEXT("DamageOverlay"));
}

void UShooterBulletCounterUI::NativeConstruct()
{
	Super::NativeConstruct();

	OnNativeVisibilityChanged.RemoveAll(this);
	OnNativeVisibilityChanged.AddUObject(
		this, &UShooterBulletCounterUI::HandleVisibilityChanged);

	if (!IsVisible())
	{
		ResetTransientFeedback();
	}
}

void UShooterBulletCounterUI::NativeDestruct()
{
	OnNativeVisibilityChanged.RemoveAll(this);
	ResetTransientFeedback();
	Super::NativeDestruct();
}

void UShooterBulletCounterUI::SetHealthPercentSilently(float LifePercent)
{
	if (WidgetTree)
	{
		if (UProgressBar* LifeBar = WidgetTree->FindWidget<UProgressBar>(TEXT("LifeBar")))
		{
			LifeBar->SetPercent(FMath::Clamp(LifePercent, 0.0f, 1.0f));
		}
	}
}

void UShooterBulletCounterUI::ResetTransientFeedback()
{
	// Damage is currently the only authored animation, but stopping every
	// transient animation also makes this safe if the Blueprint grows another
	// one later. Stop alone is insufficient because the Damage movie scene uses
	// KeepState and can leave its overlay at a visible frame.
	StopAllAnimations();

	if (Damage)
	{
		SetAnimationCurrentTime(Damage, Damage->GetEndTime());
	}

	if (WidgetTree)
	{
		if (UWidget* DamageOverlay =
			WidgetTree->FindWidget(ShooterHudWidgetNames::DamageOverlay))
		{
			// Do not force RenderOpacity to zero here. Visibility is authored by
			// the animation itself, while a persistent opacity override would make
			// the next legitimate hit feedback invisible.
			DamageOverlay->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UShooterBulletCounterUI::HandleVisibilityChanged(
	ESlateVisibility NewVisibility)
{
	if (NewVisibility == ESlateVisibility::Hidden
		|| NewVisibility == ESlateVisibility::Collapsed)
	{
		ResetTransientFeedback();
	}
}

