// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/TacticalPlayerRetargetAnimInstance.h"

#include "Tcamp_FPS.h"
#include "Animation/AnimInstanceProxy.h"
#include "Camera/CameraComponent.h"
#include "Components/WeaponInventoryComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargetProfile.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Variant_Shooter/TacticalCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace
{
	constexpr int32 LeftFingerBoneCount = 15;

	const FName LeftFingerBoneNames[LeftFingerBoneCount] = {
		TEXT("thumb_01_l"), TEXT("thumb_02_l"), TEXT("thumb_03_l"),
		TEXT("index_01_l"), TEXT("index_02_l"), TEXT("index_03_l"),
		TEXT("middle_01_l"), TEXT("middle_02_l"), TEXT("middle_03_l"),
		TEXT("ring_01_l"), TEXT("ring_02_l"), TEXT("ring_03_l"),
		TEXT("pinky_01_l"), TEXT("pinky_02_l"), TEXT("pinky_03_l")
	};

	// Final local rotations sampled from the project's existing weapon poses
	// after those poses were retargeted onto SK_SciFITrooper_Man_03. These are
	// target-skeleton values, not raw Manny rotations or guessed curl angles.
	const FQuat PistolLeftFingerGrip[LeftFingerBoneCount] = {
		FQuat(-0.538341815, 0.345544000, 0.361353077, 0.678388845),
		FQuat(0.022613737, 0.020678376, -0.191553431, 0.981003739),
		FQuat(0.008095745, 0.039528776, 0.156601685, 0.986837295),
		FQuat(-0.085778277, -0.079858059, -0.290292773, 0.949734105),
		FQuat(-0.064072129, -0.096039206, -0.239328656, 0.964050282),
		FQuat(-0.050639150, -0.026535142, -0.051508924, 0.997034767),
		FQuat(-0.073490864, -0.032573591, -0.283731782, 0.955528307),
		FQuat(-0.063577200, -0.048562499, -0.242801371, 0.966771502),
		FQuat(-0.054803276, -0.019947613, 0.004775706, 0.998286490),
		FQuat(-0.044285458, -0.024070067, -0.334590940, 0.941014523),
		FQuat(-0.055217932, -0.039561823, -0.274892899, 0.959072316),
		FQuat(0.001645738, -0.007113102, 0.034294172, 0.999385103),
		FQuat(0.014274479, -0.018390370, -0.269657065, 0.962674997),
		FQuat(-0.052395606, -0.011488560, -0.228815126, 0.971990954),
		FQuat(-0.043318379, -0.025851277, -0.112195128, 0.992404897)
	};

	const FQuat RifleLeftFingerGrip[LeftFingerBoneCount] = {
		FQuat(-0.527267485, 0.507056610, 0.181273038, 0.657284350),
		FQuat(0.042199674, 0.111262780, -0.095030464, 0.988336492),
		FQuat(0.017308304, 0.040895837, 0.278403459, 0.959437057),
		FQuat(0.071157190, -0.136749219, -0.058665048, 0.986303555),
		FQuat(-0.113751352, -0.110222385, -0.386801312, 0.908458210),
		FQuat(-0.088926848, -0.034337434, -0.175933165, 0.979775694),
		FQuat(0.086445656, -0.223308134, -0.297715563, 0.924135318),
		FQuat(-0.144741321, -0.110874369, -0.410094118, 0.893632843),
		FQuat(-0.092520124, -0.049157504, -0.071825149, 0.991899563),
		FQuat(0.062211067, -0.207919957, -0.490697173, 0.843869278),
		FQuat(-0.077342737, -0.054499367, -0.321502460, 0.942169868),
		FQuat(-0.021839075, -0.023720185, -0.014188598, 0.999379342),
		FQuat(-0.073338957, -0.004954329, -0.466625349, 0.881395306),
		FQuat(0.002527349, 0.042846662, -0.288265344, 0.956588172),
		FQuat(0.075518483, 0.072473357, -0.075925420, 0.991604709)
	};
}

UTacticalPlayerRetargetAnimInstance::UTacticalPlayerRetargetAnimInstance(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UTacticalPlayerRetargetAnimInstance::ConfigureRetarget(
	UIKRetargeter* InRetargeter,
	USkeletalMeshComponent* InSourceMeshComponent,
	const bool bMatchSourceHandTransform)
{
	bUseFirstPersonLeftHandGripIK = false;
	if (!InRetargeter || !InSourceMeshComponent || !GetSkelMeshComponent())
	{
		return false;
	}

	RetargetNode.IKRetargeterAsset = InRetargeter;
	RetargetNode.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	RetargetNode.SourceMeshComponent = InSourceMeshComponent;
	RetargetNode.bSuppressWarnings = false;
	RetargetNode.CustomRetargetProfile = FRetargetProfile{};

	if (bMatchSourceHandTransform
		&& !BuildFirstPersonWeaponGripProfile(
			InRetargeter, RetargetNode.CustomRetargetProfile))
	{
		// A renamed/removed arm chain would otherwise silently bring the visible
		// socket mismatch back. Let the character retain its normal Manny fallback.
		return false;
	}

	if (FIKRetargetProcessor* Processor = RetargetNode.GetRetargetProcessor())
	{
		Processor->SetNeedsInitialized();
	}

	const bool bInitialized =
		RetargetNode.EnsureProcessorIsInitialized(GetSkelMeshComponent());
	bUseFirstPersonLeftHandGripIK = bInitialized && bMatchSourceHandTransform;
	return bInitialized;
}

bool UTacticalPlayerRetargetAnimInstance::BuildFirstPersonWeaponGripProfile(
	const UIKRetargeter* InRetargeter,
	FRetargetProfile& OutProfile)
{
	OutProfile = FRetargetProfile{};
	if (!InRetargeter)
	{
		return false;
	}

	// Start from the asset's fully merged settings so an active asset profile is
	// respected, but publish only the arm IK and finger FK changes. Custom
	// profiles are merged every evaluation, so copying the complete stack here
	// would add needless per-frame work to the first-person presentation.
	FRetargetProfile EffectiveAssetProfile;
	EffectiveAssetProfile.FillProfileWithAssetSettings(InRetargeter);

	static const TSet<FName> FingerChainNames = {
		TEXT("LeftThumb"), TEXT("LeftIndex"), TEXT("LeftMiddle"),
		TEXT("LeftRing"), TEXT("LeftPinky"),
		TEXT("RightThumb"), TEXT("RightIndex"), TEXT("RightMiddle"),
		TEXT("RightRing"), TEXT("RightPinky")
	};
	bool bAlignedLeftArm = false;
	bool bAlignedRightArm = false;
	TSet<FName> ConfiguredFingerChains;
	for (FRetargetOpProfile& OpProfile : EffectiveAssetProfile.RetargetOpProfiles)
	{
		bool bModifiedThisOp = false;
		if (OpProfile.SettingsToApply.GetScriptStruct()
			== FIKRetargetFKChainsOpSettings::StaticStruct())
		{
			FIKRetargetFKChainsOpSettings* OpSettings =
				OpProfile.SettingsToApply.GetMutablePtr<FIKRetargetFKChainsOpSettings>();
			if (OpSettings)
			{
				for (FRetargetFKChainSettings& ChainSettings : OpSettings->ChainsToRetarget)
				{
					if (!FingerChainNames.Contains(ChainSettings.TargetChainName))
					{
						continue;
					}

					// CopyLocal transfers source local delta onto the SciFi reference
					// axes. Interpolated chain-shape retargeting is what flattened the
					// short three-bone fingers in the first-person presentation.
					ChainSettings.EnableFK = true;
					ChainSettings.RotationMode = EFKChainRotationMode::CopyLocal;
					ChainSettings.RotationAlpha = 1.0;
					ChainSettings.TranslationMode = EFKChainTranslationMode::None;
					ChainSettings.TranslationAlpha = 0.0;
					ConfiguredFingerChains.Add(ChainSettings.TargetChainName);
					bModifiedThisOp = true;
				}
			}
		}
		else if (OpProfile.SettingsToApply.GetScriptStruct()
			== FIKRetargetIKChainsOpSettings::StaticStruct())
		{
			FIKRetargetIKChainsOpSettings* OpSettings =
				OpProfile.SettingsToApply.GetMutablePtr<FIKRetargetIKChainsOpSettings>();
			if (OpSettings)
			{
				for (FRetargetIKChainSettings& ChainSettings : OpSettings->ChainsToRetarget)
				{
					if (ChainSettings.TargetChainName == TEXT("LeftArm"))
					{
						ChainSettings.EnableIK = true;
						ChainSettings.BlendToSource = 1.0;
						ChainSettings.BlendToSourceTranslation = 1.0;
						ChainSettings.BlendToSourceRotation = 1.0;
						ChainSettings.BlendToSourceWeights = FVector::OneVector;
						bAlignedLeftArm = true;
						bModifiedThisOp = true;
					}
					else if (ChainSettings.TargetChainName == TEXT("RightArm"))
					{
						ChainSettings.EnableIK = true;
						ChainSettings.BlendToSource = 1.0;
						ChainSettings.BlendToSourceTranslation = 1.0;
						ChainSettings.BlendToSourceRotation = 1.0;
						ChainSettings.BlendToSourceWeights = FVector::OneVector;
						bAlignedRightArm = true;
						bModifiedThisOp = true;
					}
				}
			}
		}

		if (bModifiedThisOp)
		{
			OutProfile.RetargetOpProfiles.Emplace(MoveTemp(OpProfile));
		}
	}

	if (!bAlignedLeftArm || !bAlignedRightArm
		|| ConfiguredFingerChains.Num() != FingerChainNames.Num())
	{
		OutProfile = FRetargetProfile{};
		return false;
	}

	return true;
}

bool UTacticalPlayerRetargetAnimInstance::BuildFirstPersonLeftHandGripEffector(
	const FTransform& SourceLeftHandComponentTransform,
	const FTransform& SourceWeaponGripComponentTransform,
	FTransform& OutEffectorTransform)
{
	OutEffectorTransform = FTransform::Identity;
	if (SourceLeftHandComponentTransform.ContainsNaN()
		|| SourceWeaponGripComponentTransform.ContainsNaN())
	{
		return false;
	}

	// Bone-space IK must not inherit component or skeletal-mesh scale. The source
	// retarget pass follows the same scale-stripping rule before processing a pose.
	FTransform LeftHand = SourceLeftHandComponentTransform;
	FTransform WeaponGrip = SourceWeaponGripComponentTransform;
	LeftHand.SetScale3D(FVector::OneVector);
	WeaponGrip.SetScale3D(FVector::OneVector);
	LeftHand.NormalizeRotation();
	WeaponGrip.NormalizeRotation();

	OutEffectorTransform = LeftHand.GetRelativeTransform(WeaponGrip);
	OutEffectorTransform.SetScale3D(FVector::OneVector);
	OutEffectorTransform.NormalizeRotation();
	return !OutEffectorTransform.ContainsNaN();
}

bool UTacticalPlayerRetargetAnimInstance::BuildFirstPersonRightHandGripEffector(
	const FTransform& SourceWeaponGripComponentTransform,
	const FTransform& TargetWeaponGripLocalTransform,
	FTransform& OutEffectorTransform)
{
	OutEffectorTransform = FTransform::Identity;
	if (SourceWeaponGripComponentTransform.ContainsNaN()
		|| TargetWeaponGripLocalTransform.ContainsNaN())
	{
		return false;
	}

	FTransform SourceGrip = SourceWeaponGripComponentTransform;
	FTransform TargetGripLocal = TargetWeaponGripLocalTransform;
	SourceGrip.SetScale3D(FVector::OneVector);
	TargetGripLocal.SetScale3D(FVector::OneVector);
	SourceGrip.NormalizeRotation();
	TargetGripLocal.NormalizeRotation();

	// SocketCS = SocketLocal * HandCS, therefore
	// HandCS = inverse(SocketLocal) * the desired SocketCS.
	OutEffectorTransform = TargetGripLocal.Inverse() * SourceGrip;
	OutEffectorTransform.SetScale3D(FVector::OneVector);
	OutEffectorTransform.NormalizeRotation();
	return !OutEffectorTransform.ContainsNaN();
}

bool UTacticalPlayerRetargetAnimInstance::BuildFirstPersonAimedRightHandGripEffector(
	const FTransform& SourceWeaponGripComponentTransform,
	const FTransform& TargetWeaponGripLocalTransform,
	const FTransform& WeaponMuzzleRelativeToGripTransform,
	const FVector& DesiredMuzzleForwardComponentSpace,
	FTransform& OutEffectorTransform)
{
	OutEffectorTransform = FTransform::Identity;
	if (SourceWeaponGripComponentTransform.ContainsNaN()
		|| TargetWeaponGripLocalTransform.ContainsNaN()
		|| WeaponMuzzleRelativeToGripTransform.ContainsNaN())
	{
		return false;
	}

	const FVector DesiredForward = DesiredMuzzleForwardComponentSpace.GetSafeNormal();
	if (DesiredForward.IsNearlyZero())
	{
		return false;
	}

	FTransform SourceGrip = SourceWeaponGripComponentTransform;
	FTransform TargetGripLocal = TargetWeaponGripLocalTransform;
	FTransform MuzzleInGrip = WeaponMuzzleRelativeToGripTransform;
	SourceGrip.SetScale3D(FVector::OneVector);
	TargetGripLocal.SetScale3D(FVector::OneVector);
	MuzzleInGrip.SetScale3D(FVector::OneVector);
	SourceGrip.NormalizeRotation();
	TargetGripLocal.NormalizeRotation();
	MuzzleInGrip.NormalizeRotation();

	// UE composes a child-space rotation as Parent * Child. First predict the
	// muzzle orientation the original Manny grip would produce, then apply the
	// shortest component-space correction that faces the camera/aim direction.
	const FQuat PredictedMuzzleRotation =
		SourceGrip.GetRotation() * MuzzleInGrip.GetRotation();
	const FVector PredictedForward =
		PredictedMuzzleRotation.RotateVector(FVector::ForwardVector).GetSafeNormal();
	if (PredictedForward.IsNearlyZero())
	{
		return false;
	}

	FQuat AimCorrection = FQuat::FindBetweenNormals(PredictedForward, DesiredForward);
	AimCorrection.Normalize();
	FQuat DesiredMuzzleRotation = AimCorrection * PredictedMuzzleRotation;
	DesiredMuzzleRotation.Normalize();
	FQuat DesiredGripRotation =
		DesiredMuzzleRotation * MuzzleInGrip.GetRotation().Inverse();
	DesiredGripRotation.Normalize();
	FQuat DesiredHandRotation =
		DesiredGripRotation * TargetGripLocal.GetRotation().Inverse();
	DesiredHandRotation.Normalize();

	// Rotate around the authored grip point: the socket remains at the source
	// Manny grip location while the hand, weapon and support hand turn together.
	const FVector DesiredHandLocation = SourceGrip.GetLocation()
		- DesiredHandRotation.RotateVector(TargetGripLocal.GetLocation());
	OutEffectorTransform = FTransform(
		DesiredHandRotation, DesiredHandLocation, FVector::OneVector);
	return !OutEffectorTransform.ContainsNaN();
}

FAnimInstanceProxy* UTacticalPlayerRetargetAnimInstance::CreateAnimInstanceProxy()
{
	return new FTacticalPlayerRetargetAnimInstanceProxy(this, &RetargetNode);
}

void UTacticalPlayerRetargetAnimInstance::DestroyAnimInstanceProxy(
	FAnimInstanceProxy* InProxy)
{
	delete static_cast<FTacticalPlayerRetargetAnimInstanceProxy*>(InProxy);
}

FTacticalPlayerRetargetAnimInstanceProxy::FTacticalPlayerRetargetAnimInstanceProxy(
	UAnimInstance* InAnimInstance,
	FAnimNode_RetargetPoseFromMesh* InRetargetNode)
	: FAnimInstanceProxy(InAnimInstance)
	, RetargetNode(InRetargetNode)
{
	LocalToComponentSpace.LocalPose.SetLinkNode(RetargetNode);
	RightHandGripIK.ComponentPose.SetLinkNode(&LocalToComponentSpace);
	LeftHandGripIK.ComponentPose.SetLinkNode(&RightHandGripIK);
	ComponentToLocalSpace.ComponentPose.SetLinkNode(&LeftHandGripIK);

	// SciFi's forearms contain additional deformation/muscle bones. FABRIK follows
	// the real hierarchy, unlike a fixed two-bone solve. Solve the weapon hand
	// first so the support hand consumes the corrected HandGrip_R coordinate.
	RightHandGripIK.RootBone.BoneName = TEXT("upperarm_r");
	RightHandGripIK.TipBone.BoneName = TEXT("hand_r");
	RightHandGripIK.EffectorTransformSpace = BCS_ComponentSpace;
	RightHandGripIK.EffectorRotationSource = BRS_CopyFromTarget;
	RightHandGripIK.Precision = 0.05f;
	RightHandGripIK.MaxIterations = 12;
	RightHandGripIK.Alpha = 0.0f;

	LeftHandGripIK.RootBone.BoneName = TEXT("upperarm_l");
	LeftHandGripIK.TipBone.BoneName = TEXT("hand_l");
	LeftHandGripIK.EffectorTarget = FBoneSocketTarget(TEXT("HandGrip_R"), true);
	LeftHandGripIK.EffectorTransformSpace = BCS_BoneSpace;
	LeftHandGripIK.EffectorRotationSource = BRS_CopyFromTarget;
	LeftHandGripIK.Precision = 0.05f;
	LeftHandGripIK.MaxIterations = 12;
	LeftHandGripIK.Alpha = 0.0f;
}

void FTacticalPlayerRetargetAnimInstanceProxy::Initialize(
	UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FTacticalPlayerRetargetAnimInstanceProxy::PreUpdate(
	UAnimInstance* InAnimInstance,
	const float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const float PreviousLeftFingerGripAlpha = LeftFingerGripAlpha;
	LeftFingerGripAlpha = 0.0f;
	bUseRifleLeftFingerGrip = false;
	RightHandGripIK.Alpha = 0.0f;
	LeftHandGripIK.Alpha = 0.0f;

	const UTacticalPlayerRetargetAnimInstance* TacticalInstance =
		Cast<UTacticalPlayerRetargetAnimInstance>(InAnimInstance);
	USkeletalMeshComponent* SourceMesh = RetargetNode
		? RetargetNode->SourceMeshComponent.Get()
		: nullptr;
	USkeletalMeshComponent* TargetMesh = InAnimInstance
		? InAnimInstance->GetSkelMeshComponent()
		: nullptr;
	if (!TacticalInstance
		|| !TacticalInstance->bUseFirstPersonLeftHandGripIK
		|| !IsValid(SourceMesh)
		|| !SourceMesh->IsRegistered()
		|| !IsValid(TargetMesh)
		|| !TargetMesh->GetSkeletalMeshAsset()
		|| SourceMesh->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE
		|| SourceMesh->GetBoneIndex(TEXT("hand_r")) == INDEX_NONE
		|| !SourceMesh->DoesSocketExist(TEXT("HandGrip_R")))
	{
		return;
	}

	const USkeletalMeshSocket* TargetGripSocket =
		TargetMesh->GetSkeletalMeshAsset()->FindSocket(TEXT("HandGrip_R"));
	if (!TargetGripSocket)
	{
		return;
	}

	const FTransform SourceGripTransform =
		SourceMesh->GetSocketTransform(TEXT("HandGrip_R"), RTS_Component);
	FTransform RightEffectorTransform;
	bool bBuiltAimedRightEffector = false;
	const ATacticalCharacter* TacticalCharacter =
		Cast<ATacticalCharacter>(InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr);
	const UWeaponInventoryComponent* Inventory = TacticalCharacter
		? TacticalCharacter->GetWeaponInventory()
		: nullptr;
	const AShooterWeapon* CurrentWeapon = Inventory
		? Inventory->CurrentWeapon.Get()
		: nullptr;
	USkeletalMeshComponent* WeaponMesh = IsValid(CurrentWeapon)
		? CurrentWeapon->GetFirstPersonMesh()
		: nullptr;
	const UCameraComponent* ViewCamera = TacticalCharacter
		? TacticalCharacter->GetFirstPersonCameraComponent()
		: nullptr;
	static const FName MuzzleSocketName(TEXT("Muzzle"));
	if (WeaponMesh && ViewCamera
		&& WeaponMesh->GetAttachParent() == TargetMesh
		&& WeaponMesh->GetAttachSocketName() == TEXT("HandGrip_R")
		&& WeaponMesh->DoesSocketExist(MuzzleSocketName))
	{
		FTransform WeaponRelativeToGrip = WeaponMesh->GetRelativeTransform();
		WeaponRelativeToGrip.SetScale3D(FVector::OneVector);
		FTransform MuzzleRelativeToWeapon =
			WeaponMesh->GetSocketTransform(MuzzleSocketName, RTS_Component);
		MuzzleRelativeToWeapon.SetScale3D(FVector::OneVector);
		const FTransform MuzzleRelativeToGrip =
			MuzzleRelativeToWeapon * WeaponRelativeToGrip;
		const FVector DesiredMuzzleForward = TargetMesh->GetComponentTransform()
			.InverseTransformVectorNoScale(ViewCamera->GetForwardVector());
		bBuiltAimedRightEffector =
			UTacticalPlayerRetargetAnimInstance::BuildFirstPersonAimedRightHandGripEffector(
				SourceGripTransform,
				TargetGripSocket->GetSocketLocalTransform(),
				MuzzleRelativeToGrip,
				DesiredMuzzleForward,
				RightEffectorTransform);
	}

	if (!bBuiltAimedRightEffector)
	{
		bBuiltAimedRightEffector =
			UTacticalPlayerRetargetAnimInstance::BuildFirstPersonRightHandGripEffector(
				SourceGripTransform,
				TargetGripSocket->GetSocketLocalTransform(),
				RightEffectorTransform);
	}
	if (bBuiltAimedRightEffector)
	{
		RightHandGripIK.EffectorTransform = RightEffectorTransform;
		RightHandGripIK.Alpha = 1.0f;
	}
	else
	{
		return;
	}

	FTransform LeftEffectorTransform;
	if (!UTacticalPlayerRetargetAnimInstance::BuildFirstPersonLeftHandGripEffector(
		SourceMesh->GetBoneTransform(TEXT("hand_l"), RTS_Component),
		SourceGripTransform,
		LeftEffectorTransform))
	{
		return;
	}

	LeftHandGripIK.EffectorTransform = LeftEffectorTransform;
	LeftHandGripIK.Alpha = 1.0f;
	bUseRifleLeftFingerGrip = CurrentWeapon && CurrentWeapon->GetWeaponCategory() != 0;
	const UAnimInstance* SourceAnimInstance = SourceMesh->GetAnimInstance();
	const UAnimMontage* ReloadMontage = CurrentWeapon
		? CurrentWeapon->GetReloadMontage()
		: nullptr;
	const bool bReloadMontagePlaying = SourceAnimInstance && ReloadMontage
		&& SourceAnimInstance->Montage_IsPlaying(ReloadMontage);
	const float TargetFingerGripAlpha =
		CurrentWeapon && !TacticalCharacter->IsDead() && !bReloadMontagePlaying
			? 1.0f
			: 0.0f;
	LeftFingerGripAlpha = FMath::FInterpTo(
		PreviousLeftFingerGripAlpha,
		TargetFingerGripAlpha,
		DeltaSeconds,
		16.0f);
	if (!bReportedLeftHandGripIKReady)
	{
		bReportedLeftHandGripIKReady = true;
		UE_LOG(LogTcamp_FPS, Display,
			TEXT("Event=FirstPersonWeaponGripIKReady Target=\"%s\" Source=\"%s\" RightRoot=upperarm_r RightTip=hand_r LeftRoot=upperarm_l LeftTip=hand_l Effector=HandGrip_R RightLocation=%s RightRotation=%s LeftOffsetLocation=%s LeftOffsetRotation=%s"),
			*GetNameSafe(InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr),
			*GetNameSafe(SourceMesh),
			*RightEffectorTransform.GetLocation().ToCompactString(),
			*RightEffectorTransform.Rotator().ToCompactString(),
			*LeftEffectorTransform.GetLocation().ToCompactString(),
			*LeftEffectorTransform.Rotator().ToCompactString());
	}
}

void FTacticalPlayerRetargetAnimInstanceProxy::CacheBones()
{
	if (!RetargetNode || !bBoneCachesInvalidated)
	{
		return;
	}

	FAnimationCacheBonesContext Context(this);
	ComponentToLocalSpace.CacheBones_AnyThread(Context);

	const FBoneContainer& RequiredBoneContainer = GetRequiredBones();
	LeftFingerBoneIndices.Reset(LeftFingerBoneCount);
	for (int32 Index = 0; Index < LeftFingerBoneCount; ++Index)
	{
		const int32 MeshPoseIndex =
			RequiredBoneContainer.GetPoseBoneIndexForBoneName(LeftFingerBoneNames[Index]);
		LeftFingerBoneIndices.Add(MeshPoseIndex != INDEX_NONE
			? RequiredBoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshPoseIndex))
			: FCompactPoseBoneIndex(INDEX_NONE));
	}
	bBoneCachesInvalidated = false;
}

bool FTacticalPlayerRetargetAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (!RetargetNode)
	{
		Output.ResetToRefPose();
		return true;
	}

	ComponentToLocalSpace.Evaluate_AnyThread(Output);
	if (LeftFingerGripAlpha > UE_KINDA_SMALL_NUMBER
		&& LeftFingerBoneIndices.Num() == LeftFingerBoneCount)
	{
		const FQuat* TargetGrip = bUseRifleLeftFingerGrip
			? RifleLeftFingerGrip
			: PistolLeftFingerGrip;
		const float BlendAlpha = FMath::Clamp(LeftFingerGripAlpha, 0.0f, 1.0f);
		for (int32 Index = 0; Index < LeftFingerBoneCount; ++Index)
		{
			const FCompactPoseBoneIndex BoneIndex = LeftFingerBoneIndices[Index];
			if (BoneIndex.GetInt() == INDEX_NONE)
			{
				continue;
			}

			FTransform& BoneTransform = Output.Pose[BoneIndex];
			FQuat BlendedRotation = FQuat::Slerp(
				BoneTransform.GetRotation(), TargetGrip[Index], BlendAlpha);
			BlendedRotation.Normalize();
			BoneTransform.SetRotation(BlendedRotation);
		}
	}
	return true;
}

void FTacticalPlayerRetargetAnimInstanceProxy::UpdateAnimationNode(
	const FAnimationUpdateContext& InContext)
{
	ComponentToLocalSpace.Update_AnyThread(InContext);
}

FAnimNode_Base* FTacticalPlayerRetargetAnimInstanceProxy::GetCustomRootNode()
{
	return &ComponentToLocalSpace;
}

void FTacticalPlayerRetargetAnimInstanceProxy::GetCustomNodes(
	TArray<FAnimNode_Base*>& OutNodes)
{
	if (RetargetNode)
	{
		OutNodes.Add(RetargetNode);
	}
	OutNodes.Add(&LocalToComponentSpace);
	OutNodes.Add(&RightHandGripIK);
	OutNodes.Add(&LeftHandGripIK);
	OutNodes.Add(&ComponentToLocalSpace);
}
