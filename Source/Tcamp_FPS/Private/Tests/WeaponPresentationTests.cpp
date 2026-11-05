// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "UObject/UnrealType.h"
#include "Weapons/TacticalWeapon.h"
#include "Variant_Shooter/Animation/TacticalRARFPAnimInstance.h"
#include "Variant_Shooter/Animation/TacticalRARWeaponAnimInstance.h"
#include "Variant_Shooter/Animation/TacticalSniperFPGripAnimInstance.h"

namespace
{
struct FWeaponPresentationExpectation
{
	const TCHAR* Label;
	const TCHAR* ClassPath;
	const TCHAR* MeshPath;
	EWeaponCategory Category;
	bool bRequiresFirstPersonLeftHandGrip;
	const TCHAR* RequiredLeftHandGripSocket;
};

const FFloatProperty* FindFloatProperty(const UObject* Object, const FName PropertyName)
{
	return Object
		? FindFProperty<FFloatProperty>(Object->GetClass(), PropertyName)
		: nullptr;
}

const FStructProperty* FindTransformProperty(const UObject* Object, const FName PropertyName)
{
	const FStructProperty* Property = Object
		? FindFProperty<FStructProperty>(Object->GetClass(), PropertyName)
		: nullptr;
	return Property && Property->Struct == TBaseStructure<FTransform>::Get()
		? Property
		: nullptr;
}

const FObjectPropertyBase* FindObjectProperty(const UObject* Object, const FName PropertyName)
{
	return Object
		? FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName)
		: nullptr;
}

bool IsFiniteUnitScaleTransform(const FTransform& Transform)
{
	return !Transform.ContainsNaN()
		&& Transform.GetRotation().IsNormalized()
		&& Transform.GetScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalWeaponPresentationConfigurationTest,
	"Tcamp.Tactical.PlayerPresentation.WeaponConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalWeaponPresentationConfigurationTest::RunTest(const FString& Parameters)
{
	static const FWeaponPresentationExpectation Expectations[] =
	{
		{
			TEXT("Pistol"),
			TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"),
			TEXT("/Game/Weapons/Pistol/Meshes/SKM_Pistol.SKM_Pistol"),
			EWeaponCategory::Pistol,
			true,
			TEXT("FP_LeftHandGrip")
		},
		{
			TEXT("Rifle"),
			TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"),
			TEXT("/Game/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle"),
			EWeaponCategory::Primary,
			true,
			TEXT("FP_LeftHandGrip")
		},
		{
			TEXT("GrenadeLauncher"),
			TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C"),
			TEXT("/Game/Weapons/GrenadeLauncher/Meshes/SKM_GrenadeLauncher.SKM_GrenadeLauncher"),
			EWeaponCategory::Primary,
			true,
			TEXT("FP_LeftHandGrip")
		}
	};
	USkeletalMesh* MannyMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	TestNotNull(TEXT("Manny first-person mesh is available"), MannyMesh);
	const USkeletalMeshSocket* MannyPalm = MannyMesh
		? MannyMesh->FindSocket(TEXT("HandGrip_L")) : nullptr;
	TestNotNull(TEXT("Manny has a HandGrip_L palm socket"), MannyPalm);
	if (MannyPalm)
	{
		TestEqual(TEXT("Manny palm socket follows hand_l"),
			MannyPalm->BoneName, FName(TEXT("hand_l")));
		TestTrue(TEXT("Manny palm offset is finite and away from the wrist"),
			!MannyPalm->GetSocketLocalTransform().ContainsNaN()
			&& MannyPalm->GetSocketLocalTransform().GetLocation().Size() > 1.0f);
	}

	for (const FWeaponPresentationExpectation& Expectation : Expectations)
	{
		UClass* WeaponClass = LoadClass<ATacticalWeapon>(
			nullptr, Expectation.ClassPath);
		TestNotNull(
			*FString::Printf(TEXT("%s Blueprint class is available"), Expectation.Label),
			WeaponClass);
		if (!WeaponClass)
		{
			continue;
		}

		const ATacticalWeapon* WeaponCDO =
			WeaponClass->GetDefaultObject<ATacticalWeapon>();
		TestNotNull(
			*FString::Printf(TEXT("%s has a valid CDO"), Expectation.Label),
			WeaponCDO);
		if (!WeaponCDO)
		{
			continue;
		}

		TestEqual(
			*FString::Printf(TEXT("%s has the expected weapon category"), Expectation.Label),
			WeaponCDO->GetWeaponCategoryEnum(), Expectation.Category);
		if (Expectation.bRequiresFirstPersonLeftHandGrip)
		{
			const FFirstPersonGripProfile Profile =
				WeaponCDO->GetFirstPersonGripProfile();
			TestTrue(*FString::Printf(TEXT("%s elbow pole favors below-frame bend"),
				Expectation.Label), Profile.ElbowDownCm > Profile.ElbowOutwardCm * 3.0f);
			TestTrue(*FString::Printf(TEXT("%s elbow pole blends with the animation"),
				Expectation.Label), Profile.ElbowPoleBiasAlpha > 0.0f
				&& Profile.ElbowPoleBiasAlpha < 1.0f);
			TestTrue(*FString::Printf(TEXT("%s first-person shoulder clears the near-camera sleeve"),
				Expectation.Label), Profile.ShoulderForwardCm >= 8.0f
				&& Profile.ShoulderForwardCm <= 15.0f);
		}

		USkeletalMesh* ExpectedMesh = LoadObject<USkeletalMesh>(
			nullptr, Expectation.MeshPath);
		TestNotNull(
			*FString::Printf(TEXT("%s presentation mesh is available"), Expectation.Label),
			ExpectedMesh);

		USkeletalMeshComponent* FirstPersonMesh = WeaponCDO->GetFirstPersonMesh();
		USkeletalMeshComponent* ThirdPersonMesh = WeaponCDO->GetThirdPersonMesh();
		TestNotNull(
			*FString::Printf(TEXT("%s has a first-person weapon mesh"), Expectation.Label),
			FirstPersonMesh);
		TestNotNull(
			*FString::Printf(TEXT("%s has a third-person weapon mesh"), Expectation.Label),
			ThirdPersonMesh);
		if (!FirstPersonMesh || !ThirdPersonMesh || !ExpectedMesh)
		{
			continue;
		}
		if (Expectation.Category == EWeaponCategory::Pistol)
		{
			const FTacticalVisualAimProfile AimProfile =
				WeaponCDO->GetFirstPersonVisualAimProfile();
			TestEqual(TEXT("Pistol ADS uses the Barrel reference bone"),
				AimProfile.RearReference, FName(TEXT("Barrel")));
			TestEqual(TEXT("Pistol ADS uses the authored Muzzle socket"),
				AimProfile.FrontReference, FName(TEXT("Muzzle")));
			TestTrue(TEXT("Pistol sight references exist on the first-person mesh"),
				FirstPersonMesh->DoesSocketExist(AimProfile.RearReference)
				&& FirstPersonMesh->DoesSocketExist(AimProfile.FrontReference));
			TestTrue(TEXT("Pistol ADS line extends forward along the gun"),
				FVector::Dist(
					FirstPersonMesh->GetSocketLocation(AimProfile.RearReference),
					FirstPersonMesh->GetSocketLocation(AimProfile.FrontReference)) > 5.0f);
			TestTrue(TEXT("Pistol virtual sight is lifted above the barrel"),
				AimProfile.RearMeshOffset.Z > 0.0f
				&& AimProfile.RearMeshOffset.Equals(AimProfile.FrontMeshOffset));
		}

		TestTrue(
			*FString::Printf(TEXT("%s first-person mesh uses the expected asset"), Expectation.Label),
			FirstPersonMesh->GetSkeletalMeshAsset() == ExpectedMesh);
		TestTrue(
			*FString::Printf(TEXT("%s third-person mesh uses the expected asset"), Expectation.Label),
			ThirdPersonMesh->GetSkeletalMeshAsset() == ExpectedMesh);
		if (Expectation.RequiredLeftHandGripSocket)
		{
			const FName GripSocketName(Expectation.RequiredLeftHandGripSocket);
			TestTrue(
				*FString::Printf(TEXT("%s mesh contains its authored left-hand grip socket"),
					Expectation.Label),
				FirstPersonMesh->DoesSocketExist(GripSocketName));
			const USkeletalMeshSocket* WeaponPalm = ExpectedMesh->FindSocket(
				GripSocketName);
			TestNotNull(*FString::Printf(TEXT("%s palm socket is authored on the mesh"),
				Expectation.Label), WeaponPalm);
			if (WeaponPalm && MannyPalm)
			{
				FTransform PalmContact = FirstPersonMesh->GetSocketTransform(
					GripSocketName, RTS_Component);
				PalmContact.SetScale3D(FVector::OneVector);
				PalmContact.NormalizeRotation();
				TestTrue(*FString::Printf(TEXT("%s palm target has a valid pose"),
					Expectation.Label), IsFiniteUnitScaleTransform(PalmContact));
				const FTransform PalmLocal = MannyPalm->GetSocketLocalTransform();
				const FQuat WristRotation = (PalmContact.GetRotation()
					* PalmLocal.GetRotation().Inverse()).GetNormalized();
				const FVector WristLocation = PalmContact.GetLocation()
					- WristRotation.RotateVector(PalmLocal.GetLocation());
				const FTransform ReconstructedPalm = PalmLocal
					* FTransform(WristRotation, WristLocation, FVector::OneVector);
				TestTrue(*FString::Printf(TEXT("%s palm contact can be reconstructed from the wrist"),
					Expectation.Label),
					ReconstructedPalm.GetLocation().Equals(PalmContact.GetLocation(), 0.01f)
					&& ReconstructedPalm.GetRotation().AngularDistance(
						PalmContact.GetRotation()) < FMath::DegreesToRadians(0.1f));
			}
		}

		const FName MuzzleSocketName = WeaponCDO->GetMuzzleSocketName();
		TestEqual(
			*FString::Printf(TEXT("%s uses the calibrated Muzzle socket"), Expectation.Label),
			MuzzleSocketName, FName(TEXT("Muzzle")));
		const FStructProperty* GameplayMuzzleProperty = FindFProperty<FStructProperty>(
			WeaponCDO->GetClass(), TEXT("GameplayMuzzleViewOffset"));
		TestNotNull(
			*FString::Printf(TEXT("%s has a stable gameplay muzzle offset"), Expectation.Label),
			GameplayMuzzleProperty);
		if (GameplayMuzzleProperty
			&& GameplayMuzzleProperty->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* GameplayOffset = GameplayMuzzleProperty->ContainerPtrToValuePtr<FVector>(
				WeaponCDO);
			TestTrue(*FString::Printf(TEXT("%s gameplay muzzle is finite and in front of the view"),
				Expectation.Label), GameplayOffset && !GameplayOffset->ContainsNaN()
				&& GameplayOffset->X > 0.0f && GameplayOffset->Z < 0.0f);
		}

		const FFloatProperty* MuzzleOffsetProperty = FindFloatProperty(
			WeaponCDO, TEXT("MuzzleOffset"));
		TestNotNull(
			*FString::Printf(TEXT("%s exposes its muzzle spawn offset"), Expectation.Label),
			MuzzleOffsetProperty);
		if (MuzzleOffsetProperty)
		{
			const float MuzzleOffset =
				MuzzleOffsetProperty->GetPropertyValue_InContainer(WeaponCDO);
			TestTrue(
				*FString::Printf(TEXT("%s has a finite non-negative muzzle spawn offset"), Expectation.Label),
				FMath::IsFinite(MuzzleOffset) && MuzzleOffset >= 0.0f);
		}

		for (USkeletalMeshComponent* Mesh : {FirstPersonMesh, ThirdPersonMesh})
		{
			const FString ViewLabel = Mesh == FirstPersonMesh
				? TEXT("first-person")
				: TEXT("third-person");
			TestTrue(
				*FString::Printf(TEXT("%s %s mesh contains its configured muzzle socket"),
					Expectation.Label, *ViewLabel),
				!MuzzleSocketName.IsNone() && Mesh->DoesSocketExist(MuzzleSocketName));
			if (MuzzleSocketName.IsNone() || !Mesh->DoesSocketExist(MuzzleSocketName))
			{
				continue;
			}

			const FTransform MuzzleTransform = Mesh->GetSocketTransform(
				MuzzleSocketName, RTS_Component);
			TestTrue(
				*FString::Printf(TEXT("%s %s muzzle calibration is finite and unscaled"),
					Expectation.Label, *ViewLabel),
				IsFiniteUnitScaleTransform(MuzzleTransform));
			TestTrue(
				*FString::Printf(TEXT("%s %s muzzle has a valid forward axis"),
					Expectation.Label, *ViewLabel),
				MuzzleTransform.GetRotation().GetForwardVector().IsNormalized());
			TestFalse(
				*FString::Printf(TEXT("%s %s muzzle is calibrated away from the weapon root"),
					Expectation.Label, *ViewLabel),
				MuzzleTransform.GetLocation().IsNearlyZero());
		}

		for (const FName OffsetPropertyName :
			{FName(TEXT("FirstPersonAttachmentOffset")), FName(TEXT("ThirdPersonAttachmentOffset"))})
		{
			const FStructProperty* OffsetProperty = FindTransformProperty(
				WeaponCDO, OffsetPropertyName);
			TestNotNull(
				*FString::Printf(TEXT("%s exposes %s"),
					Expectation.Label, *OffsetPropertyName.ToString()),
				OffsetProperty);
			if (OffsetProperty)
			{
				const FTransform* Offset = OffsetProperty->ContainerPtrToValuePtr<FTransform>(
					WeaponCDO);
				TestTrue(
					*FString::Printf(TEXT("%s %s is a valid calibration transform"),
						Expectation.Label, *OffsetPropertyName.ToString()),
					Offset && IsFiniteUnitScaleTransform(*Offset));
			}
		}

		FTransform LeftHandGrip = FTransform::Identity;
		const bool bHasLeftHandGrip =
			WeaponCDO->TryGetFirstPersonLeftHandGripTransform(LeftHandGrip);
		if (Expectation.bRequiresFirstPersonLeftHandGrip)
		{
			TestTrue(
				*FString::Printf(TEXT("%s resolves a first-person left-hand grip"), Expectation.Label),
				bHasLeftHandGrip);
			if (bHasLeftHandGrip)
			{
				if (Expectation.RequiredLeftHandGripSocket)
				{
					FTransform AuthoredPalm = FirstPersonMesh->GetSocketTransform(
						FName(Expectation.RequiredLeftHandGripSocket), RTS_Component);
					AuthoredPalm.SetScale3D(FVector::OneVector);
					AuthoredPalm.NormalizeRotation();
					TestTrue(*FString::Printf(TEXT("%s uses its authored palm socket"),
						Expectation.Label), LeftHandGrip.Equals(AuthoredPalm, 0.01f));
				}
				TestTrue(
					*FString::Printf(TEXT("%s left-hand grip is finite and unscaled"), Expectation.Label),
					IsFiniteUnitScaleTransform(LeftHandGrip));
				TestFalse(
					*FString::Printf(TEXT("%s left-hand grip is explicitly calibrated"), Expectation.Label),
					LeftHandGrip.Equals(FTransform::Identity, UE_KINDA_SMALL_NUMBER));
				TestTrue(
					*FString::Printf(TEXT("%s left-hand grip stays within weapon-local reach"), Expectation.Label),
					LeftHandGrip.GetLocation().Size() < 200.0f);
			}
		}

		const FObjectPropertyBase* FiringMontageProperty = FindObjectProperty(
			WeaponCDO, TEXT("FiringMontage"));
		TestNotNull(
			*FString::Printf(TEXT("%s exposes its firing montage configuration"), Expectation.Label),
			FiringMontageProperty);
		const UAnimMontage* FiringMontage = FiringMontageProperty
			? Cast<UAnimMontage>(FiringMontageProperty->GetObjectPropertyValue_InContainer(WeaponCDO))
			: nullptr;
		TestNotNull(
			*FString::Printf(TEXT("%s has a firing montage"), Expectation.Label),
			FiringMontage);

		if (Expectation.Category == EWeaponCategory::Pistol)
		{
			// The legacy Blueprint still serializes the template rifle montage. The
			// tactical weapon sanitizes that known pairing before the base BeginPlay
			// attaches the weapon, so lock the cook-safe replacement asset here rather
			// than treating the intentionally unrepaired source asset as the result.
			const FObjectPropertyBase* PistolFallbackProperty = FindObjectProperty(
				WeaponCDO, TEXT("PistolFireMontageFallback"));
			TestNotNull(TEXT("The pistol exposes its cook-safe firing montage fallback"),
				PistolFallbackProperty);
			const UAnimMontage* PistolFallback = PistolFallbackProperty
				? Cast<UAnimMontage>(PistolFallbackProperty->GetObjectPropertyValue_InContainer(WeaponCDO))
				: nullptr;
			TestNotNull(TEXT("The pistol firing montage fallback is loaded"), PistolFallback);
			if (PistolFallback)
			{
				const FString FallbackPath = PistolFallback->GetPathName();
				TestTrue(TEXT("The pistol fallback is a pistol firing montage"),
					FallbackPath.Contains(TEXT("Pistol"), ESearchCase::IgnoreCase));
				TestFalse(TEXT("The pistol fallback is never a rifle firing montage"),
					FallbackPath.Contains(TEXT("Rifle"), ESearchCase::IgnoreCase));
				TestTrue(TEXT("The pistol replacement differs from an inherited rifle montage"),
					!FiringMontage
					|| !FiringMontage->GetPathName().Contains(TEXT("Rifle"), ESearchCase::IgnoreCase)
					|| FiringMontage != PistolFallback);
			}
		}
	}

	// The Sniper economy tier keeps the template launcher Blueprint for gameplay,
	// but its runtime first-/third-person presentation uses this cook-referenced
	// tactical mesh. Check the asset contract independently of the old BP CDO.
	UClass* SniperClass = LoadClass<ATacticalWeapon>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C"));
	const ATacticalWeapon* SniperCDO = SniperClass
		? SniperClass->GetDefaultObject<ATacticalWeapon>() : nullptr;
	TestNotNull(TEXT("Sniper purchase class loads"), SniperCDO);
	if (SniperCDO)
	{
		TestEqual(TEXT("Sniper purchase class retains its tier"),
			SniperCDO->GetWeaponTierEnum(), EWeaponTier::Sniper);
		const FObjectPropertyBase* SniperMeshProperty = FindObjectProperty(
			SniperCDO, TEXT("SniperPresentationMesh"));
		const USkeletalMesh* SniperMesh = SniperMeshProperty
			? Cast<USkeletalMesh>(SniperMeshProperty->GetObjectPropertyValue_InContainer(SniperCDO))
			: nullptr;
		TestNotNull(TEXT("Sniper presentation mesh is cook-referenced"), SniperMesh);
		if (SniperMesh)
		{
			TestTrue(TEXT("Sniper uses the imported weapon rather than the launcher"),
				SniperMesh->GetPathName().Contains(TEXT("SKM_TacticalSniper")));
			for (const FName SocketName : {FName(TEXT("Muzzle")),
				FName(TEXT("FP_LeftHandGrip")), FName(TEXT("GripPoint_002")),
				FName(TEXT("SightRear")), FName(TEXT("SightFront"))})
			{
				TestNotNull(*FString::Printf(TEXT("Sniper mesh has %s"),
					*SocketName.ToString()), SniperMesh->FindSocket(SocketName));
			}
			for (const FName GripName : {FName(TEXT("FP_LeftHandGrip")),
				FName(TEXT("GripPoint_002"))})
			{
				const USkeletalMeshSocket* Grip = SniperMesh->FindSocket(GripName);
				if (Grip)
				{
					TestTrue(*FString::Printf(TEXT("Sniper %s reaches the front handguard"),
						*GripName.ToString()),
						Grip->GetSocketLocalTransform().GetLocation().X > 0.18f);
				}
			}
		}
		const FObjectPropertyBase* SniperReloadProperty = FindObjectProperty(
			SniperCDO, TEXT("SniperCharacterReloadMontage"));
		const UAnimMontage* SniperReload = SniperReloadProperty
			? Cast<UAnimMontage>(SniperReloadProperty->GetObjectPropertyValue_InContainer(SniperCDO))
			: nullptr;
		TestNotNull(TEXT("Sniper Manny reload is cook-referenced"), SniperReload);
		if (SniperReload && MannyMesh)
		{
			TestEqual(TEXT("Sniper reload matches Manny's skeleton"),
				SniperReload->GetSkeleton(), MannyMesh->GetSkeleton());
		}
		const FTacticalVisualAimProfile AimProfile =
			SniperCDO->GetFirstPersonVisualAimProfile();
		TestTrue(TEXT("Sniper scope mode does not pull Manny into a missing ADS pose"),
			AimProfile.RearReference.IsNone() && AimProfile.FrontReference.IsNone());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalSniperSupportHandEffectorTest,
	"Tcamp.Tactical.PlayerPresentation.SniperSupportHandEffector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalSniperSupportHandEffectorTest::RunTest(const FString& Parameters)
{
	const FTransform PalmContact(FQuat::Identity, FVector(10.0f, 20.0f, 30.0f));
	const FTransform PalmLocal(FQuat::Identity, FVector(0.0f, 5.0f, 0.0f));
	const FQuat SourceHandRotation = FRotator(0.0f, 90.0f, 0.0f).Quaternion();
	FTransform HandEffector;
	TestTrue(TEXT("Sniper support-hand effector resolves"),
		UTacticalSniperFPGripAnimInstance::BuildLeftHandEffector(
			PalmContact, PalmLocal, SourceHandRotation, HandEffector));
	TestTrue(TEXT("Sniper palm stays on gun while wrist keeps source rotation"),
		(HandEffector.TransformPosition(PalmLocal.GetLocation())
			- PalmContact.GetLocation()).IsNearlyZero(0.01f));
	TestTrue(TEXT("Sniper IK does not twist source wrist"),
		HandEffector.GetRotation().Equals(SourceHandRotation, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalRARRiflePresentationConfigurationTest,
	"Tcamp.Tactical.PlayerPresentation.RARRifleConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalRARRiflePresentationConfigurationTest::RunTest(const FString& Parameters)
{
	UClass* RifleClass = LoadClass<ATacticalWeapon>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
	TestNotNull(TEXT("Rifle Blueprint class loads"), RifleClass);
	if (!RifleClass)
	{
		return false;
	}
	const ATacticalWeapon* Rifle = RifleClass->GetDefaultObject<ATacticalWeapon>();
	USkeletalMesh* Arms = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/InfimaGames/ArtCore/Mannequin/Meshes/SK_IG_FP_Mannequin.SK_IG_FP_Mannequin"));
	USkeletalMesh* Gun = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SK_RAR_AssaultRifle.SK_RAR_AssaultRifle"));
	TestNotNull(TEXT("RAR-compatible Manny first-person arms load"), Arms);
	TestNotNull(TEXT("RAR gun loads"), Gun);
	if (Gun)
	{
		USkeletalMeshComponent* DiagnosticGun = NewObject<USkeletalMeshComponent>();
		DiagnosticGun->SetSkeletalMeshAsset(Gun);
		TestTrue(TEXT("RAR ADS can resolve both sight-line reference bones"),
			DiagnosticGun->DoesSocketExist(TEXT("SOCKET_Scope"))
			&& DiagnosticGun->DoesSocketExist(TEXT("Muzzle")));
		TestTrue(TEXT("RAR ADS line extends along the rifle barrel"),
			FVector::Dist(
				DiagnosticGun->GetSocketLocation(TEXT("SOCKET_Scope")),
				DiagnosticGun->GetSocketLocation(TEXT("Muzzle"))) > 30.0f);
	}
	if (Rifle->GetWeaponTierEnum() == EWeaponTier::Rifle)
	{
		const FTacticalVisualAimProfile AimProfile =
			Rifle->GetFirstPersonVisualAimProfile();
		TestEqual(TEXT("RAR ADS uses the scope-mount reference bone"),
			AimProfile.RearReference, FName(TEXT("SOCKET_Scope")));
		TestEqual(TEXT("RAR ADS uses the Muzzle reference"),
			AimProfile.FrontReference, FName(TEXT("Muzzle")));
		TestTrue(TEXT("RAR sight offsets compensate for unequal reference heights"),
			AimProfile.FrontMeshOffset.Z - AimProfile.RearMeshOffset.Z > 7.0f
			&& AimProfile.FrontMeshOffset.Z - AimProfile.RearMeshOffset.Z < 7.6f);
	}
	TestTrue(TEXT("Rifle uses Manny first-person arms"),
		Rifle->GetFirstPersonCharacterMeshOverride() == Arms);
	TestTrue(TEXT("Rifle uses RAR first-person gun"),
		Rifle->GetFirstPersonWeaponMeshOverride() == Gun);
	TestTrue(TEXT("Rifle uses the same RAR gun in third person"),
		Rifle->GetThirdPersonWeaponMeshOverride() == Gun);
	TestTrue(TEXT("RAR gun provides the legacy third-person support-hand socket"),
		Gun && Gun->FindSocket(TEXT("GripPoint_002")) != nullptr);
	TestTrue(TEXT("RAR gun provides a muzzle socket for both viewpoints"),
		Gun && Gun->FindSocket(TEXT("Muzzle")) != nullptr);
	TestEqual(TEXT("Rifle uses the RAR authored mount socket"),
		Rifle->GetFirstPersonHolderSocketOverride(), FName(TEXT("SOCKET_Weapon")));
	TestTrue(TEXT("RAR arms provide the mount socket"),
		Arms && Arms->FindSocket(TEXT("SOCKET_Weapon")) != nullptr);
	TestTrue(TEXT("RAR arms use the native action graph"),
		Rifle->GetFirstPersonAnimInstanceClass().Get()
			== UTacticalRARFPAnimInstance::StaticClass());
	TestTrue(TEXT("RAR gun uses the native action graph"),
		Rifle->GetFirstPersonWeaponAnimInstanceClass().Get()
			== UTacticalRARWeaponAnimInstance::StaticClass());
	const UTacticalRARFPAnimInstance* RARArmsAnim = GetDefault<UTacticalRARFPAnimInstance>();
	const UAnimSequence* IdlePose = RARArmsAnim->GetIdlePoseSequence();
	const UAnimSequence* AimPose = RARArmsAnim->GetAimPoseSequence();
	const UAnimSequence* AimIn = RARArmsAnim->GetAimInTransitionSequence();
	const UAnimSequence* AimOut = RARArmsAnim->GetAimOutTransitionSequence();
	const UBlendSpace* Movement = RARArmsAnim->GetLocomotionBlendSpace();
	const UAnimSequence* AimedIdle = RARArmsAnim->GetAimedIdleLoopSequence();
	const UAnimSequence* AimedWalk = RARArmsAnim->GetAimedWalkLoopSequence();
	const UAnimSequence* Falling = RARArmsAnim->GetFallingLoopSequence();
	TestTrue(TEXT("RAR arms have a non-additive base pose"),
		IdlePose && !IdlePose->IsValidAdditive());
	TestTrue(TEXT("RAR ADS has a non-additive sight pose and authored transitions"),
		AimPose && !AimPose->IsValidAdditive()
		&& AimIn && AimIn->IsValidAdditive()
		&& AimOut && AimOut->IsValidAdditive());
	TestTrue(TEXT("RAR aimed idle and walk are additive over the sight pose"),
		AimedIdle && AimedIdle->IsValidAdditive()
		&& AimedWalk && AimedWalk->IsValidAdditive());
	TestTrue(TEXT("RAR movement is additive over the base pose"),
		Movement && Movement->IsValidAdditive());
	TestTrue(TEXT("RAR falling is additive over the base pose"),
		Falling && Falling->IsValidAdditive());
	const UAnimMontage* ArmsFire = Rifle->GetFirstPersonFiringMontage();
	const UAnimMontage* ArmsReload = Rifle->GetFirstPersonReloadMontage();
	const UAnimMontage* GunFire = Rifle->GetFirstPersonWeaponFiringMontage();
	const UAnimMontage* GunReload = Rifle->GetFirstPersonWeaponReloadMontage();
	TestTrue(TEXT("RAR character action montages match the RAR arms skeleton"),
		Arms && ArmsFire && ArmsReload
		&& ArmsFire->GetSkeleton() == Arms->GetSkeleton()
		&& ArmsReload->GetSkeleton() == Arms->GetSkeleton());
	TestTrue(TEXT("RAR gun action montages match the RAR gun skeleton"),
		Gun && GunFire && GunReload
		&& GunFire->GetSkeleton() == Gun->GetSkeleton()
		&& GunReload->GetSkeleton() == Gun->GetSkeleton());
	const FBoolProperty* PartsProperty = FindFProperty<FBoolProperty>(
		Rifle->GetClass(), TEXT("bUseRARDefaultFirstPersonParts"));
	TestTrue(TEXT("RAR default parts are configured"),
		PartsProperty && PartsProperty->GetPropertyValue_InContainer(Rifle));
	for (const FName PartPropertyName : {
		FName(TEXT("RARDefaultMagazineMesh")),
		FName(TEXT("RARDefaultForestockMesh")),
		FName(TEXT("RARDefaultFrontSightMesh"))})
	{
		const FObjectPropertyBase* PartProperty = FindObjectProperty(
			Rifle, PartPropertyName);
		TestTrue(*FString::Printf(TEXT("RAR default part %s is loaded"),
			*PartPropertyName.ToString()),
			PartProperty && PartProperty->GetObjectPropertyValue_InContainer(Rifle));
	}
	return true;
}

#endif
