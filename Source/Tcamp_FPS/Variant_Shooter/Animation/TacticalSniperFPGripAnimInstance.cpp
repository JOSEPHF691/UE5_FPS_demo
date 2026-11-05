// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/Animation/TacticalSniperFPGripAnimInstance.h"

#include "Tcamp_FPS.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace
{
	const FName PalmSocketName(TEXT("HandGrip_L"));
	const FName LeftHandBoneName(TEXT("hand_l"));
}

bool UTacticalSniperFPGripAnimInstance::Configure(
	USkeletalMeshComponent* InSourceMesh,
	USkeletalMeshComponent* InWeaponMesh,
	FName InGripSocketName,
	bool bInLockWristToWeapon)
{
	USkeletalMeshComponent* OverlayMesh = GetSkelMeshComponent();
	if (!IsValid(OverlayMesh) || !IsValid(InSourceMesh)
		|| !IsValid(InWeaponMesh) || OverlayMesh == InSourceMesh
		|| !OverlayMesh->GetSkeletalMeshAsset()
		|| !InSourceMesh->GetSkeletalMeshAsset()
		|| OverlayMesh->GetSkeletalMeshAsset()->GetSkeleton()
			!= InSourceMesh->GetSkeletalMeshAsset()->GetSkeleton()
		|| !OverlayMesh->DoesSocketExist(PalmSocketName)
		|| InGripSocketName.IsNone()
		|| !InWeaponMesh->DoesSocketExist(InGripSocketName))
	{
		UE_LOG(LogTcamp_FPS, Warning,
			TEXT("Event=SniperFirstPersonGripIKConfigureFailed Overlay=\"%s\" Source=\"%s\" Weapon=\"%s\""),
			*GetNameSafe(OverlayMesh), *GetNameSafe(InSourceMesh),
			*GetNameSafe(InWeaponMesh));
		SourceMesh.Reset();
		WeaponMesh.Reset();
		return false;
	}

	SourceMesh = InSourceMesh;
	WeaponMesh = InWeaponMesh;
	GripSocketName = InGripSocketName;
	bLockWristToWeapon = bInLockWristToWeapon;
	return true;
}

bool UTacticalSniperFPGripAnimInstance::BuildLeftHandEffector(
	const FTransform& PalmContactComponentSpace,
	const FTransform& PalmSocketLocalToHand,
	const FQuat& SourceHandRotationComponentSpace,
	FTransform& OutHandComponentSpace)
{
	OutHandComponentSpace = FTransform::Identity;
	if (PalmContactComponentSpace.ContainsNaN()
		|| PalmSocketLocalToHand.ContainsNaN()
		|| SourceHandRotationComponentSpace.ContainsNaN())
	{
		return false;
	}

	FTransform Contact = PalmContactComponentSpace;
	FTransform PalmLocal = PalmSocketLocalToHand;
	FQuat HandRotation = SourceHandRotationComponentSpace;
	Contact.SetScale3D(FVector::OneVector);
	PalmLocal.SetScale3D(FVector::OneVector);
	Contact.NormalizeRotation();
	PalmLocal.NormalizeRotation();
	HandRotation.Normalize();

	// The FABRIK node preserves the incoming wrist rotation. Account for the
	// palm socket's positional offset using that same rotation, not the gun's
	// currently untuned socket rotation; otherwise the palm retains a gap.
	const FVector HandLocation = Contact.GetLocation()
		- HandRotation.RotateVector(PalmLocal.GetLocation());
	OutHandComponentSpace = FTransform(
		HandRotation, HandLocation, FVector::OneVector);
	return !OutHandComponentSpace.ContainsNaN();
}

FAnimInstanceProxy* UTacticalSniperFPGripAnimInstance::CreateAnimInstanceProxy()
{
	return new FTacticalSniperFPGripAnimInstanceProxy(this);
}

void UTacticalSniperFPGripAnimInstance::DestroyAnimInstanceProxy(
	FAnimInstanceProxy* InProxy)
{
	delete static_cast<FTacticalSniperFPGripAnimInstanceProxy*>(InProxy);
}

FTacticalSniperFPGripAnimInstanceProxy::FTacticalSniperFPGripAnimInstanceProxy(
	UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
	LocalToComponentSpace.LocalPose.SetLinkNode(&CopySourcePose);
	LeftHandIK.ComponentPose.SetLinkNode(&LocalToComponentSpace);
	ComponentToLocalSpace.ComponentPose.SetLinkNode(&LeftHandIK);

	// Include Manny's clavicle in the chain. The imported sniper foregrip is
	// about 10 cm beyond what the upperarm-only chain can reach; clamping the
	// hand there made the IK report success while the palm visibly held air.
	LeftHandIK.RootBone.BoneName = TEXT("clavicle_l");
	LeftHandIK.TipBone.BoneName = LeftHandBoneName;
	LeftHandIK.EffectorTransformSpace = BCS_ComponentSpace;
	// Grip socket orientations differ between the imported long guns. Move the
	// palm into contact without forcing either orientation onto Manny's wrist.
	LeftHandIK.EffectorRotationSource = BRS_KeepComponentSpaceRotation;
	LeftHandIK.Precision = 0.05f;
	LeftHandIK.MaxIterations = 12;
	LeftHandIK.Alpha = 0.0f;
}

void FTacticalSniperFPGripAnimInstanceProxy::Initialize(
	UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FTacticalSniperFPGripAnimInstanceProxy::PreUpdate(
	UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	// Set the source before the base proxy calls CopyPoseFromMesh::PreUpdate.
	const UTacticalSniperFPGripAnimInstance* Instance =
		Cast<UTacticalSniperFPGripAnimInstance>(InAnimInstance);
	USkeletalMeshComponent* Source = Instance ? Instance->SourceMesh.Get() : nullptr;
	const FName GripSocket = Instance ? Instance->GripSocketName : NAME_None;
	CopySourcePose.SourceMeshComponent = IsValid(Source) ? Source : nullptr;
	LeftHandIK.Alpha = 0.0f;
	LeftHandIK.EffectorRotationSource = Instance && Instance->bLockWristToWeapon
		? BRS_CopyFromTarget : BRS_KeepComponentSpaceRotation;
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	USkeletalMeshComponent* Overlay = InAnimInstance
		? InAnimInstance->GetSkelMeshComponent() : nullptr;
	USkeletalMeshComponent* Weapon = Instance ? Instance->WeaponMesh.Get() : nullptr;
	if (!IsValid(Source) || !Source->IsRegistered()
		|| !IsValid(Overlay) || !Overlay->IsRegistered()
		|| !IsValid(Weapon) || !Weapon->IsRegistered()
		|| Source->GetBoneIndex(LeftHandBoneName) == INDEX_NONE
		|| !Overlay->GetSkeletalMeshAsset()
		|| GripSocket.IsNone()
		|| !Weapon->DoesSocketExist(GripSocket)
		|| Weapon->GetAttachParent() != Source)
	{
		return;
	}

	const USkeletalMeshSocket* PalmSocket =
		Overlay->GetSkeletalMeshAsset()->FindSocket(PalmSocketName);
	if (!PalmSocket || PalmSocket->BoneName != LeftHandBoneName)
	{
		return;
	}

	FTransform ContactCS = Weapon->GetSocketTransform(
		GripSocket, RTS_World).GetRelativeTransform(
			Overlay->GetComponentTransform());
	ContactCS.SetScale3D(FVector::OneVector);
	ContactCS.NormalizeRotation();
	FQuat SourceHandRotationCS = Overlay->GetComponentQuat().Inverse()
		* Source->GetBoneTransform(LeftHandBoneName, RTS_World).GetRotation();
	if (Instance->bLockWristToWeapon)
	{
		// The source ABP_FP_Weapon swings the left wrist by tens of degrees
		// during locomotion while the right-hand-mounted sniper barely moves.
		// Keep the idle support-grip orientation in gun space so the fingertips
		// do not skate around a palm position that is already IK-correct.
		// This measured Manny/sniper rest orientation is used only for the owner
		// viewmodel; the shared third-person rifle/sniper overlay is unchanged.
		const FQuat IdleHandInSniper = FRotator(5.3f, 20.0f, -81.7f).Quaternion();
		SourceHandRotationCS = Overlay->GetComponentQuat().Inverse()
			* Weapon->GetComponentQuat() * IdleHandInSniper;
	}
	SourceHandRotationCS.Normalize();
	FTransform HandCS;
	if (!UTacticalSniperFPGripAnimInstance::BuildLeftHandEffector(
		ContactCS, PalmSocket->GetSocketLocalTransform(),
		SourceHandRotationCS, HandCS))
	{
		return;
	}

	const AShooterWeapon* ShooterWeapon = Cast<AShooterWeapon>(Weapon->GetOwner());
	const UAnimInstance* SourceAnim = Source->GetAnimInstance();
	const UAnimMontage* FirstPersonReload = ShooterWeapon
		? ShooterWeapon->GetFirstPersonReloadMontage() : nullptr;
	const UAnimMontage* ThirdPersonReload = ShooterWeapon
		? ShooterWeapon->GetThirdPersonReloadMontage() : nullptr;
	const bool bReloading = SourceAnim
		&& ((FirstPersonReload && SourceAnim->Montage_IsPlaying(FirstPersonReload))
			|| (ThirdPersonReload && SourceAnim->Montage_IsPlaying(ThirdPersonReload)));
	// Keep the authored reload hand motion visible. The support hand returns to
	// the weapon gradually after the montage instead of snapping to its socket.
	GripAlpha = FMath::FInterpTo(GripAlpha, bReloading ? 0.0f : 1.0f,
		DeltaSeconds, 20.0f);
	LeftHandIK.EffectorTransform = HandCS;
	LeftHandIK.Alpha = GripAlpha;
	if (!bReportedReady)
	{
		bReportedReady = true;
		UE_LOG(LogTcamp_FPS, Display,
			TEXT("Event=SniperFirstPersonGripIKReady Overlay=\"%s\" Source=\"%s\" Weapon=\"%s\" Effector=%s"),
			*GetNameSafe(Overlay), *GetNameSafe(Source), *GetNameSafe(Weapon),
			*HandCS.GetLocation().ToCompactString());
	}
}

bool FTacticalSniperFPGripAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	ComponentToLocalSpace.Evaluate_AnyThread(Output);
	return true;
}

void FTacticalSniperFPGripAnimInstanceProxy::UpdateAnimationNode(
	const FAnimationUpdateContext& InContext)
{
	ComponentToLocalSpace.Update_AnyThread(InContext);
}

FAnimNode_Base* FTacticalSniperFPGripAnimInstanceProxy::GetCustomRootNode()
{
	return &ComponentToLocalSpace;
}

void FTacticalSniperFPGripAnimInstanceProxy::GetCustomNodes(
	TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&CopySourcePose);
	OutNodes.Add(&LocalToComponentSpace);
	OutNodes.Add(&LeftHandIK);
	OutNodes.Add(&ComponentToLocalSpace);
}
