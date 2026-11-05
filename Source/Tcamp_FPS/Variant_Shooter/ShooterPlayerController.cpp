// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "ShooterPlayerState.h"
#include "ShooterUI.h"
#include "ShooterBulletCounterUI.h"
#include "UI/ShooterCombatFeedbackUI.h"
#include "Components/HealthComponent.h"
#include "Components/WeaponInventoryComponent.h"
#include "ShooterWeapon.h"
#include "Tcamp_FPS.h"
#include "Camera/PlayerCameraManager.h"

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController())
	{
		if (BulletCounterUIClass)
		{
			BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);
			if (BulletCounterUI)
			{
				BulletCounterUI->AddToViewport(10);
			}
		}
		if (ShooterUIClass)
		{
			ShooterUI = CreateWidget<UShooterUI>(this, ShooterUIClass);
			if (ShooterUI)
			{
				ShooterUI->AddToViewport(10);
			}
		}

		// Possession may have been acknowledged before these widgets existed.
		// Rebinding is idempotent and immediately seeds newly-created HUD widgets
		// from the retained replicated pawn state after reconnect.
		if (APawn* ControlledPawn = GetPawn())
		{
			BindHUDDelegates(ControlledPawn);
		}
	}
}

void AShooterPlayerController::BindHUDDelegates(APawn* InPawn)
{
	if (!InPawn)
	{
		return;
	}

	InPawn->OnDestroyed.RemoveDynamic(this, &AShooterPlayerController::OnPawnDestroyed);
	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	if (AShooterPlayerState* PS = GetPlayerState<AShooterPlayerState>())
	{
		PS->OnKillsUpdated.RemoveDynamic(this, &AShooterPlayerController::OnKillsUpdated);
		PS->OnKillsUpdated.AddDynamic(this, &AShooterPlayerController::OnKillsUpdated);
		PS->OnMatchOver.RemoveDynamic(this, &AShooterPlayerController::OnMatchOver);
		PS->OnMatchOver.AddDynamic(this, &AShooterPlayerController::OnMatchOver);
	}

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		if (UHealthComponent* HealthComp = ShooterCharacter->GetHealthComponent())
		{
			if (BoundHUDHealthComponent.Get() != HealthComp)
			{
				if (UHealthComponent* PreviousHealth =
					BoundHUDHealthComponent.Get())
				{
					PreviousHealth->OnDamaged.RemoveDynamic(
						this, &AShooterPlayerController::OnHealthDamaged);
					PreviousHealth->OnHealed.RemoveDynamic(
						this, &AShooterPlayerController::OnHealthHealed);
				}

				BoundHUDHealthComponent = HealthComp;
				if (BulletCounterUI)
				{
					BulletCounterUI->ResetTransientFeedback();
				}
				if (CombatFeedbackUI)
				{
					CombatFeedbackUI->ResetTransientFeedback();
				}
			}

			HealthComp->OnDamaged.RemoveDynamic(this, &AShooterPlayerController::OnHealthDamaged);
			HealthComp->OnDamaged.AddDynamic(this, &AShooterPlayerController::OnHealthDamaged);
			HealthComp->OnHealed.RemoveDynamic(this, &AShooterPlayerController::OnHealthHealed);
			HealthComp->OnHealed.AddDynamic(this, &AShooterPlayerController::OnHealthHealed);
			SynchronizeHealthHUD(HealthComp->GetHealthPercent());
		}

		if (UWeaponInventoryComponent* WeaponInv = ShooterCharacter->GetWeaponInventory())
		{
			WeaponInv->OnAmmoUpdated.RemoveDynamic(this, &AShooterPlayerController::OnAmmoUpdated);
			WeaponInv->OnAmmoUpdated.AddDynamic(this, &AShooterPlayerController::OnAmmoUpdated);

			WeaponInv->OnWeaponChanged.RemoveDynamic(this, &AShooterPlayerController::OnWeaponChanged);
			WeaponInv->OnWeaponChanged.AddDynamic(this, &AShooterPlayerController::OnWeaponChanged);

			// Bind hit feedback to the currently equipped weapon
			if (WeaponInv->CurrentWeapon)
			{
				WeaponInv->CurrentWeapon->OnEnemyHit.RemoveDynamic(this, &AShooterPlayerController::OnEnemyHit);
				WeaponInv->CurrentWeapon->OnEnemyHit.AddDynamic(this, &AShooterPlayerController::OnEnemyHit);
				OnAmmoUpdated(WeaponInv->CurrentWeapon->GetBulletCount(), WeaponInv->CurrentWeapon->GetMagazineSize(), WeaponInv->CurrentWeapon->GetReserveBulletCount());
			}
			else
			{
				// Do not leave a newly-created widget showing its design-time full
				// magazine while the replicated weapon reference is still resolving.
				OnAmmoUpdated(0, 0, 0);
			}
		}
	}
}

void AShooterPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
	BindHUDDelegates(InPawn);
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindHUDDelegates(InPawn);
}

void AShooterPlayerController::OnEnemyHit()
{
	if (IsValid(CombatFeedbackUI))
	{
		CombatFeedbackUI->ShowHitConfirmation();
	}
	else if (IsValid(ShooterUI))
	{
		ShooterUI->BP_OnEnemyHit();
	}
}

void AShooterPlayerController::EnsureCombatFeedbackUI()
{
	if (!IsLocalPlayerController() || IsValid(CombatFeedbackUI))
	{
		return;
	}

	CombatFeedbackUI = CreateWidget<UShooterCombatFeedbackUI>(
		this, UShooterCombatFeedbackUI::StaticClass());
	if (CombatFeedbackUI)
	{
		CombatFeedbackUI->AddToPlayerScreen(5);
		CombatFeedbackUI->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void AShooterPlayerController::NotifyEnemyHitConfirmed()
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsLocalController())
	{
		OnEnemyHit();
	}
	else
	{
		ClientNotifyEnemyHitConfirmed();
	}
}

void AShooterPlayerController::ClientNotifyEnemyHitConfirmed_Implementation()
{
	OnEnemyHit();
}

void AShooterPlayerController::NotifyDamageReceived(
	const FVector& WorldDirectionToSource,
	float Strength)
{
	if (!HasAuthority())
	{
		return;
	}

	const FVector SafeWorldDirection = WorldDirectionToSource.GetSafeNormal();
	const float SafeStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	if (IsLocalController())
	{
		PresentDamageReceived(SafeWorldDirection, SafeStrength);
	}
	else
	{
		ClientNotifyDamageReceived(SafeWorldDirection, SafeStrength);
	}
}

void AShooterPlayerController::ClientNotifyDamageReceived_Implementation(
	FVector_NetQuantizeNormal WorldDirectionToSource,
	float Strength)
{
	PresentDamageReceived(WorldDirectionToSource, Strength);
}

void AShooterPlayerController::PresentDamageReceived(
	const FVector& WorldDirectionToSource,
	float Strength)
{
	if (!IsValid(CombatFeedbackUI) || !CanPresentDamageFeedback())
	{
		return;
	}

	if (WorldDirectionToSource.IsNearlyZero())
	{
		CombatFeedbackUI->ShowDamageFeedback(Strength);
		return;
	}

	const float ViewYawDegrees = IsValid(PlayerCameraManager)
		? PlayerCameraManager->GetCameraRotation().Yaw
		: GetControlRotation().Yaw;
	const FVector2D ScreenDirection =
		ShooterCombatFeedbackStyle::ComputeDamageScreenDirection(
			WorldDirectionToSource,
			ViewYawDegrees);
	if (ScreenDirection.IsNearlyZero())
	{
		CombatFeedbackUI->ShowDamageFeedback(Strength);
		return;
	}

	CombatFeedbackUI->ShowDirectionalDamageFeedback(ScreenDirection, Strength);
}

bool AShooterPlayerController::CanPresentDamageFeedback() const
{
	const AShooterCharacter* ControlledCharacter =
		Cast<AShooterCharacter>(GetPawn());
	return IsLocalController()
		&& ControlledCharacter
		&& !ControlledCharacter->IsDead()
		&& GetViewTarget() == ControlledCharacter;
}

void AShooterPlayerController::OnWeaponChanged(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon)
{
	if (OldWeapon)
	{
		OldWeapon->OnEnemyHit.RemoveDynamic(this, &AShooterPlayerController::OnEnemyHit);
	}
	if (NewWeapon)
	{
		NewWeapon->OnEnemyHit.RemoveDynamic(this, &AShooterPlayerController::OnEnemyHit);
		NewWeapon->OnEnemyHit.AddDynamic(this, &AShooterPlayerController::OnEnemyHit);
		// CurrentWeapon commonly resolves after possession on a reconnecting
		// client. Seed the HUD as soon as it does instead of waiting for the next
		// shot/reload to produce an ammo delegate.
		OnAmmoUpdated(NewWeapon->GetBulletCount(), NewWeapon->GetMagazineSize(),
			NewWeapon->GetReserveBulletCount());
	}
	else
	{
		OnAmmoUpdated(0, 0, 0);
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	if (const AShooterCharacter* DestroyedCharacter =
		Cast<AShooterCharacter>(DestroyedActor))
	{
		if (UHealthComponent* DestroyedHealth =
			DestroyedCharacter->GetHealthComponent();
			DestroyedHealth && BoundHUDHealthComponent.Get() == DestroyedHealth)
		{
			DestroyedHealth->OnDamaged.RemoveDynamic(
				this, &AShooterPlayerController::OnHealthDamaged);
			DestroyedHealth->OnHealed.RemoveDynamic(
				this, &AShooterPlayerController::OnHealthHealed);
			BoundHUDHealthComponent.Reset();
		}
	}

	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->ResetTransientFeedback();
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
		BulletCounterUI->BP_UpdateAmmoCounter(0, 0, 0);
	}
	if (IsValid(CombatFeedbackUI))
	{
		CombatFeedbackUI->ResetTransientFeedback();
	}

	if (!HasAuthority() || !ShouldAutoRespawnPawn() || !CharacterClass)
	{
		return;
	}

	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), ActorList);

	if (ActorList.Num() > 0)
	{
		AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];
		const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

		if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
		{
			Possess(RespawnedCharacter);
		}
	}
}

void AShooterPlayerController::OnHealthDamaged(float HealthPercent)
{
	SynchronizeHealthHUD(HealthPercent);
	// Directional damage feedback is delivered separately from the server.
	// Health replication owns only the persistent bar value so reconnecting
	// players cannot replay a stale transient hit flash.
}

void AShooterPlayerController::OnHealthHealed(float HealthPercent)
{
	SynchronizeHealthHUD(HealthPercent);
}

void AShooterPlayerController::SynchronizeHealthHUD(float HealthPercent)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->SetHealthPercentSilently(HealthPercent);
	}
}

void AShooterPlayerController::OnAmmoUpdated(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo)
{
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, CurrentAmmo);
		BulletCounterUI->BP_UpdateAmmoCounter(MagazineSize, CurrentAmmo, ReserveAmmo);
	}
}

void AShooterPlayerController::OnKillsUpdated(int32 Kills, int32 KillGoal)
{
	if (ShooterUI)
	{
		ShooterUI->BP_UpdateKills(Kills, KillGoal);
	}
}

void AShooterPlayerController::OnMatchOver()
{
	if (!ShooterUI)
	{
		return;
	}

	if (AShooterPlayerState* PS = GetPlayerState<AShooterPlayerState>())
	{
		if (PS->bIsWinner)
		{
			ShooterUI->BP_OnVictory();
		}
		else
		{
			ShooterUI->BP_OnDefeat();
		}
	}
}
