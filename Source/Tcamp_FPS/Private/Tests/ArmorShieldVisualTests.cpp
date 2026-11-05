// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Armor/ArmorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArmorShieldVisualFallbackTest,
	"Tcamp.Tactical.PlayerPresentation.ArmorShieldVisualFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FArmorShieldVisualFallbackTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient);
	UArmorComponent* Armor = NewObject<UArmorComponent>(Owner, TEXT("TestArmor"));
	Owner->AddInstanceComponent(Armor);
	UStaticMeshComponent* BlueprintSphere = NewObject<UStaticMeshComponent>(Owner, TEXT("ShieldSphere"));
	Owner->AddInstanceComponent(BlueprintSphere);

	TestFalse(TEXT("An empty Blueprint shield cannot render"),
		UArmorComponent::HasRenderableShieldVisual(BlueprintSphere));
	TestTrue(TEXT("The native shield has both a mesh and material"),
		UArmorComponent::HasRenderableShieldVisual(Armor->ShieldMesh));
	TestEqual(TEXT("Empty ShieldSphere falls back to the native mesh"),
		Armor->ResolveShieldVisual(), Armor->ShieldMesh.Get());

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* ShieldMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Variant_Shooter/Materials/M_Shield.M_Shield"));
	TestNotNull(TEXT("Sphere asset is available"), SphereMesh);
	TestNotNull(TEXT("Shield material is available"), ShieldMaterial);
	if (SphereMesh && ShieldMaterial)
	{
		BlueprintSphere->SetStaticMesh(SphereMesh);
		BlueprintSphere->SetMaterial(0, ShieldMaterial);
		TestTrue(TEXT("A complete Blueprint shield can render"),
			UArmorComponent::HasRenderableShieldVisual(BlueprintSphere));
		// Clear the cache so a newly completed authored visual can be selected.
		Armor->ActiveShieldMesh = nullptr;
		TestEqual(TEXT("A complete Blueprint shield takes priority"),
			Armor->ResolveShieldVisual(), BlueprintSphere);

		BlueprintSphere->SetStaticMesh(nullptr);
		TestEqual(TEXT("If the active Blueprint mesh becomes empty, native fallback recovers"),
			Armor->ResolveShieldVisual(), Armor->ShieldMesh.Get());
	}
	return true;
}

#endif
