#include "Effects/TacticalBodyHitEffect.h"

#include "Effects/TacticalBodyHitRules.h"
#include "TacticalCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTacticalBodyHitFxEnabled(
		TEXT("tc.BodyHitFx.Enabled"), 1,
		TEXT("Enable local cosmetic body-hit impacts. 0 disables them for visual/performance comparison."),
		ECVF_Default);
}

namespace TacticalBodyHitRules
{
	bool IsEnemyContact(const AActor* Victim, const AController* EventInstigator)
	{
		const APawn* Attacker = EventInstigator ? EventInstigator->GetPawn() : nullptr;
		if (!Victim || !Attacker || Attacker == Victim)
		{
			return false;
		}

		const ATacticalCharacter* VictimPlayer = Cast<ATacticalCharacter>(Victim);
		const ATacticalCharacter* AttackerPlayer = Cast<ATacticalCharacter>(Attacker);
		if (VictimPlayer && AttackerPlayer)
		{
			if (VictimPlayer->GetTeam() <= 1 && VictimPlayer->GetTeam() == AttackerPlayer->GetTeam())
			{
				return false;
			}
		}
		return true;
	}

	bool TryResolvePointImpact(const ACharacter* Victim, const FDamageEvent& DamageEvent,
		FVector& OutPoint, FVector& OutNormal, FName& OutBone)
	{
		if (!Victim || !DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			return false;
		}

		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		const USkeletalMeshComponent* Mesh = Victim->GetMesh();
		const FVector Center = Mesh ? Mesh->Bounds.Origin : Victim->GetActorLocation();
		const float Radius = Mesh ? FMath::Max(100.0f, Mesh->Bounds.SphereRadius * 2.0f) : 100.0f;
		OutPoint = PointEvent.HitInfo.ImpactPoint;
		const bool bInvalidPoint = OutPoint.ContainsNaN()
			|| FVector::DistSquared(OutPoint, Center) > FMath::Square(Radius);
		OutNormal = PointEvent.HitInfo.ImpactNormal.GetSafeNormal();
		if (bInvalidPoint || OutNormal.IsNearlyZero())
		{
			OutNormal = -PointEvent.ShotDirection.GetSafeNormal();
		}
		if (OutNormal.IsNearlyZero())
		{
			OutNormal = Victim->GetActorForwardVector();
		}
		if (bInvalidPoint)
		{
			// Explosion overlap hits can carry the projectile location instead
			// of a body surface. Put that one cue on the visible side of the mesh.
			const float SurfaceOffset = Mesh
				? FMath::Clamp(Mesh->Bounds.BoxExtent.GetMax() * 0.3f, 18.0f, 40.0f)
				: 30.0f;
			OutPoint = Center + OutNormal * SurfaceOffset;
		}
		OutBone = PointEvent.HitInfo.BoneName;
		return true;
	}
}

ATacticalBodyHitEffect::ATacticalBodyHitEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetReplicateMovement(false);

	ImpactCard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ImpactCard"));
	SetRootComponent(ImpactCard);
	ImpactCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ImpactCard->SetGenerateOverlapEvents(false);
	ImpactCard->SetCastShadow(false);
	ImpactCard->bAffectDynamicIndirectLighting = false;
	ImpactCard->SetCanEverAffectNavigation(false);
	ArmorHaloCard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArmorHaloCard"));
	ArmorHaloCard->SetupAttachment(ImpactCard);
	ArmorHaloCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArmorHaloCard->SetGenerateOverlapEvents(false);
	ArmorHaloCard->SetCastShadow(false);
	ArmorHaloCard->bAffectDynamicIndirectLighting = false;
	ArmorHaloCard->SetCanEverAffectNavigation(false);
	ArmorHaloCard->SetVisibility(false);
	// The cards share a plane normal but sit on separate depths to avoid z-fighting.
	ArmorHaloCard->SetRelativeLocation(FVector(0.0f, 0.0f, -4.0f));
	ArmorHaloCard->SetRelativeScale3D(FVector(1.8f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneAsset.Succeeded())
	{
		ImpactCard->SetStaticMesh(PlaneAsset.Object);
		ArmorHaloCard->SetStaticMesh(PlaneAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ImpactMaterial(
		TEXT("/Game/Tactical/FX/M_TacticalBodyHit.M_TacticalBodyHit"));
	if (ImpactMaterial.Succeeded())
	{
		ImpactCard->SetMaterial(0, ImpactMaterial.Object);
		ArmorHaloCard->SetMaterial(0, ImpactMaterial.Object);
	}
}

void ATacticalBodyHitEffect::SpawnForVictim(ACharacter* Victim, const uint8 EffectFlags,
	const FVector& ImpactPoint, const FVector& ImpactNormal, const FName BoneName)
{
	if (!IsValid(Victim) || (EffectFlags & (TacticalBodyHitRules::Health | TacticalBodyHitRules::Armor)) == 0)
	{
		return;
	}
	if (CVarTacticalBodyHitFxEnabled.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = Victim->GetWorld();
	APlayerController* LocalViewer = World ? World->GetFirstPlayerController() : nullptr;
	if (!LocalViewer || !LocalViewer->IsLocalController() || LocalViewer->GetPawn() == Victim)
	{
		return;
	}

	const FVector ViewerLocation = LocalViewer->PlayerCameraManager
		? LocalViewer->PlayerCameraManager->GetCameraLocation() : LocalViewer->GetPawn() ?
		LocalViewer->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	constexpr float CullDistance = 18000.0f;
	if (FVector::DistSquared(ViewerLocation, ImpactPoint) > FMath::Square(CullDistance)
		|| FVector::DistSquared(ViewerLocation, ImpactPoint) < FMath::Square(40.0f))
	{
		return;
	}

	USkeletalMeshComponent* VictimMesh = Victim->GetMesh();
	if (!VictimMesh)
	{
		return;
	}

	const FVector SurfaceNormal = ImpactNormal.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
	const FVector SpawnLocation = ImpactPoint + SurfaceNormal * 1.5f;
	const FRotator SpawnRotation = FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ATacticalBodyHitEffect* Effect = World->SpawnActor<ATacticalBodyHitEffect>(
		ATacticalBodyHitEffect::StaticClass(), SpawnLocation, SpawnRotation, Params);
	if (!Effect || !Effect->ImpactCard || !Effect->ImpactCard->GetMaterial(0))
	{
		if (Effect)
		{
			Effect->Destroy();
		}
		return;
	}

	const FName AttachBone = !BoneName.IsNone() && VictimMesh->DoesSocketExist(BoneName)
		? BoneName : NAME_None;
	Effect->AttachToComponent(VictimMesh, FAttachmentTransformRules::KeepWorldTransform, AttachBone);

	const bool bHealth = (EffectFlags & TacticalBodyHitRules::Health) != 0;
	const bool bArmor = (EffectFlags & TacticalBodyHitRules::Armor) != 0;
	const bool bHeadshot = (EffectFlags & TacticalBodyHitRules::Headshot) != 0;
	const FLinearColor ImpactColor = bHealth ? FLinearColor(1.0f, 0.075f, 0.025f)
		: FLinearColor(0.12f, 0.65f, 1.0f);
	Effect->DynamicMaterial = Effect->ImpactCard->CreateDynamicMaterialInstance(0);
	if (!Effect->DynamicMaterial)
	{
		Effect->Destroy();
		return;
	}
	Effect->DynamicMaterial->SetVectorParameterValue(TEXT("ImpactColor"), ImpactColor);
	Effect->DynamicMaterial->SetScalarParameterValue(TEXT("ImpactBrightness"), bHeadshot ? 5.0f : 3.5f);
	Effect->DynamicMaterial->SetScalarParameterValue(TEXT("ImpactOpacity"), 1.0f);
	Effect->TotalTime = bHeadshot ? 0.20f : 0.16f;
	Effect->RemainingTime = Effect->TotalTime;
	Effect->BaseOpacity = 1.0f;
	// A mixed hit needs blue beyond the red center, not only the red card.
	const bool bMixedHit = bHealth && bArmor;
	Effect->ImpactCard->SetWorldScale3D(FVector(bMixedHit ? (bHeadshot ? 0.15f : 0.12f)
		: (bHeadshot ? 0.19f : bHealth ? 0.15f : 0.12f)));
	if (bMixedHit && Effect->ArmorHaloCard && Effect->ArmorHaloCard->GetMaterial(0))
	{
		Effect->ArmorDynamicMaterial = Effect->ArmorHaloCard->CreateDynamicMaterialInstance(0);
		if (Effect->ArmorDynamicMaterial)
		{
			Effect->ArmorDynamicMaterial->SetVectorParameterValue(
				TEXT("ImpactColor"), FLinearColor(0.12f, 0.65f, 1.0f));
			Effect->ArmorDynamicMaterial->SetScalarParameterValue(
				TEXT("ImpactBrightness"), bHeadshot ? 3.6f : 2.7f);
			Effect->ArmorDynamicMaterial->SetScalarParameterValue(
				TEXT("ImpactOpacity"), Effect->ArmorBaseOpacity);
			Effect->ArmorHaloCard->SetVisibility(true);
		}
	}
	Effect->SetLifeSpan(Effect->TotalTime + 0.05f);
}

void ATacticalBodyHitEffect::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RemainingTime = FMath::Max(0.0f, RemainingTime - DeltaSeconds);
	if (DynamicMaterial && TotalTime > KINDA_SMALL_NUMBER)
	{
		const float Alpha = FMath::Clamp(RemainingTime / TotalTime, 0.0f, 1.0f);
		DynamicMaterial->SetScalarParameterValue(TEXT("ImpactOpacity"), BaseOpacity * Alpha * Alpha);
		if (ArmorDynamicMaterial)
		{
			ArmorDynamicMaterial->SetScalarParameterValue(
				TEXT("ImpactOpacity"), ArmorBaseOpacity * Alpha * Alpha);
		}
	}
}
