// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "TacticalRARWeaponAnimInstance.generated.h"

class UAnimSequence;

/**
 * Native graph for the RAR rifle's separate skeletal mesh. The weapon's
 * fire/reload montages animate its bolt and magazine independently from the
 * first-person character montage.
 */
USTRUCT()
struct TCAMP_FPS_API FTacticalRARWeaponAnimInstanceProxy final : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FTacticalRARWeaponAnimInstanceProxy() = default;
	explicit FTacticalRARWeaponAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &MontageSlot; }

private:
	FAnimNode_SequencePlayer_Standalone ReferencePose;
	FAnimNode_Slot MontageSlot;
};

UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API UTacticalRARWeaponAnimInstance final : public UAnimInstance
{
	GENERATED_BODY()

public:
	UTacticalRARWeaponAnimInstance();

	UAnimSequence* GetReferenceSequence() const { return ReferenceSequence; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ReferenceSequence;
};
