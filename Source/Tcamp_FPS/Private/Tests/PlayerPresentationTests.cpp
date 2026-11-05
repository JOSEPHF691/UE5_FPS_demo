// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/TacticalPlayerRetargetAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Variant_Shooter/TacticalCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPlayerPresentationConfigurationTest,
	"Tcamp.Tactical.PlayerPresentation.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalPlayerPresentationConfigurationTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* MannyMesh = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	TestNotNull(TEXT("The Manny player mesh is available"), MannyMesh);
	if (MannyMesh)
	{
		const USkeletalMeshSocket* MannyGrip = MannyMesh->FindSocket(TEXT("HandGrip_R"));
		TestNotNull(TEXT("Manny contains the right-hand weapon socket"), MannyGrip);
		if (MannyGrip)
		{
			TestEqual(TEXT("Manny's weapon socket is parented to hand_r"),
				MannyGrip->BoneName, FName(TEXT("hand_r")));
			TestFalse(TEXT("Manny's weapon socket transform is valid"),
				MannyGrip->GetSocketLocalTransform().ContainsNaN());
		}
	}

	UClass* TacticalCharacterBlueprintClass = LoadClass<ATacticalCharacter>(
		nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/BP_TacticalCharacter.BP_TacticalCharacter_C"));
	TestNotNull(TEXT("The tactical character Blueprint class is available"),
		TacticalCharacterBlueprintClass);
	ATacticalCharacter* CharacterCDO = TacticalCharacterBlueprintClass
		? TacticalCharacterBlueprintClass->GetDefaultObject<ATacticalCharacter>()
		: nullptr;
	TestNotNull(TEXT("The tactical character Blueprint has a valid CDO"), CharacterCDO);
	if (!CharacterCDO)
	{
		return false;
	}

	USkeletalMeshComponent* ThirdPersonManny = CharacterCDO->GetMesh();
	USkeletalMeshComponent* FirstPersonManny = CharacterCDO->GetFirstPersonMesh();
	USceneComponent* ViewPivot = CharacterCDO->GetFirstPersonViewPivot();
	UCameraComponent* ViewCamera = CharacterCDO->GetFirstPersonCameraComponent();
	TestNotNull(TEXT("The third-person Manny component exists"), ThirdPersonManny);
	TestNotNull(TEXT("The first-person Manny component exists"), FirstPersonManny);
	TestNotNull(TEXT("The first-person eye pivot exists"), ViewPivot);
	TestNotNull(TEXT("The first-person camera exists"), ViewCamera);
	if (!ThirdPersonManny || !FirstPersonManny || !ViewPivot || !ViewCamera)
	{
		return false;
	}
	TestTrue(TEXT("The eye pivot is attached to the upright capsule"),
		ViewPivot->GetAttachParent() == CharacterCDO->GetCapsuleComponent());
	TestTrue(TEXT("The camera uses controller view rotation"),
		ViewCamera->bUsePawnControlRotation);
	// The authored Blueprint starts with the camera on the head socket. At
	// BeginPlay the C++ view rig moves camera and arms under the eye pivot.
	TestTrue(TEXT("The authored camera starts on the arms head socket"),
		ViewCamera->GetAttachParent() == FirstPersonManny
		&& ViewCamera->GetAttachSocketName() == FName(TEXT("head")));

	TestTrue(TEXT("The third-person body directly renders Manny"),
		ThirdPersonManny->GetSkeletalMeshAsset() == MannyMesh);
	TestTrue(TEXT("The first-person body directly renders Manny"),
		FirstPersonManny->GetSkeletalMeshAsset() == MannyMesh);
	TestTrue(TEXT("Both perspectives use the same skeleton"),
		ThirdPersonManny->GetSkeletalMeshAsset()
			&& FirstPersonManny->GetSkeletalMeshAsset()
			&& ThirdPersonManny->GetSkeletalMeshAsset()->GetSkeleton()
				== FirstPersonManny->GetSkeletalMeshAsset()->GetSkeleton());
	TestTrue(TEXT("The first-person Manny is attached to the character body"),
		FirstPersonManny->GetAttachParent() == ThirdPersonManny);

	TestTrue(TEXT("The first-person Manny is owner-only"), FirstPersonManny->bOnlyOwnerSee);
	TestFalse(TEXT("The first-person Manny is not hidden from its owner"),
		FirstPersonManny->bOwnerNoSee);
	TestEqual(TEXT("The first-person Manny uses the first-person render path"),
		FirstPersonManny->FirstPersonPrimitiveType,
		EFirstPersonPrimitiveType::FirstPerson);
	TestEqual(TEXT("The first-person Manny has no gameplay collision"),
		FirstPersonManny->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("The first-person Manny does not add a duplicate shadow"),
		FirstPersonManny->CastShadow);

	TestTrue(TEXT("The third-person Manny is hidden from its owner"),
		ThirdPersonManny->bOwnerNoSee);
	TestFalse(TEXT("The third-person Manny remains visible to other players"),
		ThirdPersonManny->bOnlyOwnerSee);
	TestEqual(TEXT("The third-person Manny uses the world render path"),
		ThirdPersonManny->FirstPersonPrimitiveType,
		EFirstPersonPrimitiveType::WorldSpaceRepresentation);

	TestTrue(TEXT("The first-person weapon always attaches to Manny"),
		CharacterCDO->ResolveFirstPersonWeaponAttachmentMesh() == FirstPersonManny);
	TestTrue(TEXT("The first-person Manny exposes HandGrip_R"),
		FirstPersonManny->DoesSocketExist(TEXT("HandGrip_R")));
	TestTrue(TEXT("The third-person Manny exposes HandGrip_R"),
		ThirdPersonManny->DoesSocketExist(TEXT("HandGrip_R")));

	TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	CharacterCDO->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
	TestEqual(TEXT("The player has only its direct first- and third-person meshes"),
		SkeletalMeshComponents.Num(), 2);
	for (USkeletalMeshComponent* Component : SkeletalMeshComponents)
	{
		if (!Component)
		{
			continue;
		}

		TestTrue(
			*FString::Printf(TEXT("%s uses Manny"), *Component->GetName()),
			Component->GetSkeletalMeshAsset() == MannyMesh);
		TestTrue(
			*FString::Printf(TEXT("%s does not run the retired SciFi retarget bridge"),
				*Component->GetName()),
			Component->GetAnimClass() != UTacticalPlayerRetargetAnimInstance::StaticClass());
	}

	return true;
}

#endif
