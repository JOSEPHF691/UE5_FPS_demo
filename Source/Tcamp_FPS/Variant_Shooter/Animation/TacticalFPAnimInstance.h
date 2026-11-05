// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "TacticalFPAnimInstance.generated.h"

UCLASS(BlueprintType, Blueprintable)
class TCAMP_FPS_API UTacticalFPAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	bool bIsAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimBlendAlpha = 0.0f;
};
