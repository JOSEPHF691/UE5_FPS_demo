// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Economy/EconomyTypes.h"
#include "TreasureChest.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class USceneComponent;
class UMaterialInstanceDynamic;
class AShooterNPC;
class ATacticalCharacter;
class ATacticalPlayerState;

/**
 *  Treasure chest guarded by AI.
 *  When both guards are killed, one player may hold interact to claim a team reward.
 */
UCLASS()
class TCAMP_FPS_API ATreasureChest : public AActor
{
	GENERATED_BODY()

	/** Visual mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* ChestMesh;

	/**
	 * Code-owned placeholder used only while ChestMesh has no authored asset.
	 * It keeps the objective visible in PIE and packaged builds without preventing
	 * a future art mesh from replacing it through the existing ChestMesh component.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Fallback", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FallbackBodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Fallback", meta = (AllowPrivateAccess = "true"))
	USceneComponent* FallbackLidHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Fallback", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FallbackLidMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Fallback", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FallbackBandLeftMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Fallback", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FallbackBandRightMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Fallback", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* FallbackLockMesh;

	/** Interaction zone */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USphereComponent* InteractionZone;

protected:
	/** Guard NPC class to spawn. Exactly two guards are created for an active chest. */
	UPROPERTY(EditAnywhere, Category = "Chest")
	TSubclassOf<AShooterNPC> GuardClass;

	UPROPERTY(EditAnywhere, Category = "Chest", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ClaimDuration = TacticalEconomy::TreasureClaimDuration;

	/** Every guard spawned this activation, including corpses awaiting deferred destruction. */
	UPROPERTY()
	TArray<TObjectPtr<AShooterNPC>> AllSpawnedGuards;

	/** Guards that have not yet delivered their one authoritative death notification. */
	UPROPERTY()
	TArray<TObjectPtr<AShooterNPC>> LivingGuards;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveForRound, BlueprintReadOnly, Category = "Chest")
	bool bIsActiveForRound = false;

	/** True when the guard is dead and chest is openable */
	UPROPERTY(ReplicatedUsing = OnRep_Openable, BlueprintReadOnly, Category = "Chest")
	bool bIsOpenable = false;

	/** True after a player has looted the chest */
	UPROPERTY(ReplicatedUsing = OnRep_Looted, BlueprintReadOnly, Category = "Chest")
	bool bIsLooted = false;

	/** Fixed team reward; legacy enum values are retained only for asset compatibility. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Chest")
	EChestReward CurrentReward = EChestReward::TeamMoneyAndHeal;

	/** Public claimant identity and synchronized completion timestamp. */
	UPROPERTY(ReplicatedUsing = OnRep_ClaimState, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<ATacticalPlayerState> ClaimingPlayerState;

	/** Replicated actor reference used only to mirror optional Blueprint claim visuals. */
	UPROPERTY(ReplicatedUsing = OnRep_ClaimState)
	TObjectPtr<ATacticalCharacter> ClaimingCharacter;

	UPROPERTY(ReplicatedUsing = OnRep_ClaimState, BlueprintReadOnly, Category = "Chest")
	float ClaimEndServerTime = 0.0f;

	/** Monotonic per-actor sequence so a fast cancel/reclaim cannot merge visually. */
	UPROPERTY(ReplicatedUsing = OnRep_ClaimState)
	uint8 ClaimSerial = 0;

	/** Retained through the claimed state so remote clients receive the loot visual context. */
	UPROPERTY(ReplicatedUsing = OnRep_Looted)
	TObjectPtr<ATacticalCharacter> LastLooter;

	/** Monotonic per-actor sequence for exactly-once remote loot presentation. */
	UPROPERTY(ReplicatedUsing = OnRep_Looted)
	uint8 LootSerial = 0;

	/** Client-only fences preventing duplicate optional Blueprint events. */
	TWeakObjectPtr<ATacticalCharacter> PresentedClaimant;
	uint8 PresentedClaimSerial = 0;
	uint8 PresentedLootSerial = 0;
	bool bClaimPresentationActive = false;
	bool bLootPresentationActive = false;

	FTimerHandle ClaimTimer;
	FTimerHandle ClaimValidationTimer;

	int32 PreparedRound = INDEX_NONE;
	uint8 ActiveSiteIndex = 255;
	uint8 ClaimedTeam = 255;
	bool bRewardCommitted = false;
	bool bGuardSpawnInProgress = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FallbackWoodMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FallbackMetalMaterial;

public:
	ATreasureChest();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called when a guard NPC is killed */
	void NotifyGuardKilled(AShooterNPC* KilledGuard);

	/** Clears prior state and prepares this hidden template for one round. */
	void PrepareForRound(int32 RoundNumber, uint8 SiteIndex);

	/** Spawns exactly two guards. Fails closed if either spawn is invalid. */
	bool ActivateForRound();

	/** Cancels interaction, destroys guards, and hides the chest. */
	void DeactivateForRound();

	/** Compatibility wrapper retained for existing callers/assets. */
	void ResetForRound(bool bActive);

	/** Starts/cancels the authoritative hold-to-claim timer. */
	bool BeginClaim(ATacticalCharacter* Claimant);
	void CancelClaim(ATacticalCharacter* Claimant);

	float GetClaimDuration() const { return ClaimDuration; }
	int32 GetPreparedRound() const { return PreparedRound; }
	uint8 GetActiveSiteIndex() const { return ActiveSiteIndex; }
	int32 GetLivingGuardCount() const { return LivingGuards.Num(); }
	ATacticalPlayerState* GetClaimingPlayerState() const { return ClaimingPlayerState.Get(); }
	float GetClaimEndServerTime() const { return ClaimEndServerTime; }
	uint8 GetClaimedTeam() const { return ClaimedTeam; }
	bool IsActiveForRound() const { return bIsActiveForRound; }
	bool IsOpenable() const { return bIsOpenable; }
	bool IsLooted() const { return bIsLooted; }

	// ---- Blueprint Events ----

	UFUNCTION(BlueprintImplementableEvent, Category = "Chest", meta = (DisplayName = "On Chest Unlocked"))
	void BP_OnChestUnlocked();

	UFUNCTION(BlueprintImplementableEvent, Category = "Chest", meta = (DisplayName = "On Chest Looted"))
	void BP_OnChestLooted(AActor* Looter, EChestReward Reward);

	UFUNCTION(BlueprintImplementableEvent, Category = "Chest", meta = (DisplayName = "On Chest Closed"))
	void BP_OnChestClosed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Chest", meta = (DisplayName = "On Claim Started"))
	void BP_OnClaimStarted(AActor* Claimant, float Duration);

	UFUNCTION(BlueprintImplementableEvent, Category = "Chest", meta = (DisplayName = "On Claim Cancelled"))
	void BP_OnClaimCancelled(AActor* Claimant);

protected:
	/** Spawn exactly two guard NPCs around the chest. */
	bool SpawnGuards();

	/** Apply team money and claimant healing. */
	void ApplyReward(AActor* Looter);
	void CompleteClaim();
	void ValidateActiveClaim();
	void ReleaseClaim(bool bNotifyBlueprint);
	void DestroySpawnedGuards();
	void PublishAuthoritativeState();
	bool IsClaimantValid(const ATacticalCharacter* Claimant) const;
	void ApplyActiveState();
	void ConfigureVisualComponents();
	void InitializeFallbackMaterials();
	void RefreshInteractionOffers();
	void ClearInteractionOffers();

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UFUNCTION()
	void OnRep_ActiveForRound();

	UFUNCTION()
	void OnRep_Openable();

	UFUNCTION()
	void OnRep_Looted();

	UFUNCTION()
	void OnRep_ClaimState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
