// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Economy/EconomyTypes.h"
#include "Engine/DataTable.h"
#include "ArmorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnArmorChangedDelegate, EArmorType, ArmorType, float, DurabilityPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShieldHitDelegate, float, DurabilityPercent, bool, bPenetrated);

/**
 *  Armor component — DataTable-driven damage reduction.
 *  Looks up FArmorDataRow on equip for reduction values and max durability.
 */
class UStaticMeshComponent;

UCLASS(ClassGroup = (Shooter), meta = (BlueprintSpawnableComponent))
class TCAMP_FPS_API UArmorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UArmorComponent();

	// ---- Configuration ----

	/** DataTable of FArmorDataRow — assigned in Blueprint */
	UPROPERTY(EditAnywhere, Category = "Armor|Data")
	TSoftObjectPtr<UDataTable> ArmorDataTable;

	// ---- Shield Visual ----

	/** Translucent oval mesh that flashes when armor absorbs a hit */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Armor|Visual")
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	/** Resolved Blueprint ShieldSphere, or the native ShieldMesh fallback. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ActiveShieldMesh;

	/** How long the shield stays visible after a hit (seconds) */
	UPROPERTY(EditAnywhere, Category = "Armor|Visual", meta = (ClampMin = 0.05f, ClampMax = 2.0f))
	float ShieldHitShowDuration = 0.3f;

	// ---- Replicated State ----

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Armor")
	EArmorType ArmorType = EArmorType::None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Armor")
	float CurrentDurability = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Armor")
	float MaxDurability = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_ArmorState)
	uint8 ArmorStateSerial = 0;

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Armor")
	FOnArmorChangedDelegate OnArmorChanged;

	UPROPERTY(BlueprintAssignable, Category = "Armor")
	FOnShieldHitDelegate OnShieldHit;

	// ---- API ----

	float ProcessIncomingDamage(float IncomingDamage, bool bHeadshot, float& OutAbsorbed);

	float ProcessIncomingProjectileDamage(float IncomingDamage, bool bHeadshot, AActor* DamageCauser, float& OutAbsorbed);

	/** Equip armor — looks up FArmorDataRow from DataTable */
	void EquipArmor(EArmorType Type);

	/** Restore a previously saved armor type and absolute durability. */
	void RestoreArmor(EArmorType Type, float Durability);

	void BreakArmor();

	bool HasArmor() const;

	UFUNCTION(BlueprintPure, Category = "Armor")
	float GetDurabilityPercent() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Armor", meta = (DisplayName = "Shield Hit"))
	void BP_ShieldHit(float DurabilityPercent, bool bPenetrated);

	// ---- Shield Visual ----

	/** Shows the shield mesh briefly — called via multicast RPC */
	UFUNCTION(NetMulticast, Unreliable, Category = "Armor|Visual")
	void MulticastShieldHitEffect();

	void ShowShieldMesh();
	void HideShieldMesh();
	UStaticMeshComponent* ResolveShieldVisual();
	/** A named Blueprint component is only usable when it can actually draw the shield. */
	static bool HasRenderableShieldVisual(const UStaticMeshComponent* Candidate);
	FTimerHandle ShieldHideTimer;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void CacheArmorData(EArmorType Type);
	float BodyDamageReduction = 0.0f;
	float HeadDamageReduction = 0.0f;
	virtual void BeginPlay() override;
	void PrepareNativeShieldVisual();

	UFUNCTION()
	void OnRep_ArmorState();
};
