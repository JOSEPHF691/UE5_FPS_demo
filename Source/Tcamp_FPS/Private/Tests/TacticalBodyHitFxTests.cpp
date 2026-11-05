// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Effects/TacticalBodyHitEffect.h"
#include "Effects/TacticalBodyHitRules.h"
#include "Variant_Shooter/TacticalCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Components/StaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalBodyHitResolutionTest,
	"Tcamp.Tactical.PlayerPresentation.BodyHitResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalBodyHitResolutionTest::RunTest(const FString& Parameters)
{
	using namespace TacticalBodyHitRules;

	TestEqual(TEXT("HP loss alone produces a health impact"),
		Classify(100.0f, 75.0f, 0.0f, 0.0f, false, true), Health);
	TestEqual(TEXT("Armor absorption alone produces an armor impact"),
		Classify(100.0f, 100.0f, 50.0f, 40.0f, false, true), Armor);
	TestEqual(TEXT("An armor-penetrating hit shows both effects"),
		Classify(100.0f, 85.0f, 50.0f, 40.0f, false, true),
		static_cast<uint8>(Health | Armor));
	TestEqual(TEXT("An armored headshot preserves both impact types and headshot flag"),
		Classify(100.0f, 75.0f, 50.0f, 30.0f, true, true),
		static_cast<uint8>(Health | Armor | Headshot));
	TestEqual(TEXT("A health-only headshot is visibly distinguished"),
		Classify(100.0f, 50.0f, 0.0f, 0.0f, true, true),
		static_cast<uint8>(Health | Headshot));
	TestEqual(TEXT("No applied damage never shows a body-hit effect"),
		Classify(100.0f, 100.0f, 50.0f, 50.0f, false, true), None);
	TestEqual(TEXT("A headshot contact without damage is not a false hit"),
		Classify(100.0f, 100.0f, 50.0f, 50.0f, true, true), None);
	TestEqual(TEXT("Healing and armor restoration are not hits"),
		Classify(50.0f, 80.0f, 20.0f, 35.0f, false, true), None);
	TestEqual(TEXT("The enemy-only gate suppresses friendly visual feedback"),
		Classify(100.0f, 70.0f, 50.0f, 35.0f, true, false), None);
	TestFalse(TEXT("Missing victim and instigator cannot be an enemy contact"),
		IsEnemyContact(nullptr, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalBodyHitNetworkContractTest,
	"Tcamp.Tactical.PlayerPresentation.BodyHitNetworkContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalBodyHitNetworkContractTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("The short-lived body-hit visual never replicates"),
		ATacticalBodyHitEffect::StaticClass()
			->GetDefaultObject<ATacticalBodyHitEffect>()->GetIsReplicated());
	const ATacticalBodyHitEffect* EffectCDO = ATacticalBodyHitEffect::StaticClass()
		->GetDefaultObject<ATacticalBodyHitEffect>();
	const UStaticMeshComponent* ImpactCard = Cast<UStaticMeshComponent>(EffectCDO->GetRootComponent());
	TestNotNull(TEXT("The body-hit effect has a local impact card"), ImpactCard);
	if (ImpactCard)
	{
		TestNotNull(TEXT("The body-hit material is a cooked hard reference"), ImpactCard->GetMaterial(0));
	}
	TArray<UStaticMeshComponent*> EffectCards;
	EffectCDO->GetComponents<UStaticMeshComponent>(EffectCards);
	TestEqual(TEXT("One local actor includes separate core and armor-halo cards"),
		EffectCards.Num(), 2);
	for (const UStaticMeshComponent* Card : EffectCards)
	{
		TestNotNull(TEXT("Both visual layers use the cooked body-hit material"),
			Card ? Card->GetMaterial(0) : nullptr);
	}

	UClass* DamageVictimClasses[] = {
		ATacticalCharacter::StaticClass(),
		AShooterNPC::StaticClass()
	};
	for (UClass* VictimClass : DamageVictimClasses)
	{
		const UFunction* Event = VictimClass->FindFunctionByName(
			TEXT("MulticastResolvedBodyHit"));
		const FString ClassName = VictimClass->GetName();
		TestNotNull(FString::Printf(TEXT("%s has a resolved body-hit event"), *ClassName), Event);
		if (Event)
		{
			TestTrue(FString::Printf(TEXT("%s broadcasts to relevant clients"), *ClassName),
				Event->HasAnyFunctionFlags(FUNC_NetMulticast));
			TestFalse(FString::Printf(TEXT("%s uses an unreliable cosmetic event"), *ClassName),
				Event->HasAnyFunctionFlags(FUNC_NetReliable));
		}
	}
	return true;
}

#endif
