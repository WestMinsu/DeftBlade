#include "Character/DBEnemyCharacter.h"

#include "AI/DBEnemyAIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/DBCollisionChannels.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UI/DBHealthBarWidget.h"

ADBEnemyCharacter::ADBEnemyCharacter()
{
	AIControllerClass = ADBEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	GetCapsuleComponent()->InitCapsuleSize(42, 96);
	GetCapsuleComponent()->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Overlap);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	auto* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0, 540, 0);
	Movement->MaxWalkSpeed = 340;
	// The shared AnimBP enters locomotion from speed and acceleration, including AI path following.
	Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;
	Movement->bUseRVOAvoidance = true;
	Movement->AvoidanceConsiderationRadius = 400;
	Movement->GravityScale = 2.0f;
	CombatComponent = CreateDefaultSubobject<UDBCombatComponent>(TEXT("Combat"));
	HealthComponent = CreateDefaultSubobject<UDBHealthComponent>(TEXT("Health"));
	Movement->AddTickPrerequisiteComponent(CombatComponent);
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthDisplay = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthDisplay"));
	HealthDisplay->SetupAttachment(GetRootComponent());
	HealthDisplay->SetRelativeLocation(FVector(0, 0, 130));
	HealthDisplay->SetWidgetSpace(EWidgetSpace::Screen);
	HealthDisplay->SetDrawSize(FVector2D(100, 10));
	HealthDisplay->SetPivot(FVector2D(0.5, 1.0));
	HealthDisplay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADBEnemyCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CombatComponent->InitializeWeapon(WeaponMesh);
}

void ADBEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	CombatComponent->InitializeWeapon(WeaponMesh);
	CombatComponent->OnParried.AddUObject(this, &ThisClass::ReactToParry);
	HealthComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	HealthDisplay->InitWidget();
	if (auto* Widget = Cast<UDBHealthBarWidget>(HealthDisplay->GetUserWidgetObject()))
	{
		Widget->BindHealth(HealthComponent);
	}
}

bool ADBEnemyCharacter::CanAct() const
{
	return !HealthComponent->IsDead() && ReactionInstanceId == INDEX_NONE && GetCharacterMovement()->IsMovingOnGround()
	       && GetWorld()->GetTimeSeconds() >= HitStunEndsAt && GetWorld()->GetTimeSeconds() >= LandingRecoveryEndsAt;
}

EDBAttackId ADBEnemyCharacter::SelectAttack(float, bool bCombo) const
{
	return bCombo ? EDBAttackId::Heavy1 : EDBAttackId::Light1;
}

bool ADBEnemyCharacter::HasStaggerResistance() const
{
	return CombatComponent->HasPendingParryLesson() || CombatComponent->HasAttackStaggerResistance();
}

bool ADBEnemyCharacter::CanBeLaunched() const
{
	return !HealthComponent->IsDead() && !CombatComponent->HasPendingParryLesson();
}

void ADBEnemyCharacter::BeginDropArrival()
{
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	bRecoverAfterLanding = true;
	bArrivingFromDrop = true;
}

void ADBEnemyCharacter::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (GetCharacterMovement()->IsFalling())
	{
		StopReaction();
		if (bRecoverAfterLanding && AirHitReactionMontage)
		{
			if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
			{
				Anim->Montage_Play(AirHitReactionMontage);
			}
		}
	}
	else if (GetCharacterMovement()->IsMovingOnGround())
	{
		CombatComponent->HandleLanding(GetCharacterMovement()->CurrentFloor.HitResult);
		if (PreviousMovementMode == MOVE_Falling)
		{
			StopReaction();
		}
		if (PreviousMovementMode == MOVE_Falling && bRecoverAfterLanding)
		{
			const float Duration = bArrivingFromDrop ? 2.0f : LandingRecoveryDuration;
			LandingRecoveryEndsAt = GetWorld()->GetTimeSeconds() + Duration;
			GetCharacterMovement()->StopMovementImmediately();
			if (UAnimInstance* Anim = GetMesh()->GetAnimInstance();
			    Anim && LandingRecoveryMontage && Duration > 0.0f && LandingRecoveryMontage->RateScale > 0.0f)
			{
				const float PlayRate = bArrivingFromDrop ? 1.0f
				                                         : LandingRecoveryMontage->GetPlayLength()
				                                               / (Duration * LandingRecoveryMontage->RateScale);
				Anim->Montage_Play(LandingRecoveryMontage, PlayRate);
			}
		}
		bRecoverAfterLanding = false;
		bArrivingFromDrop = false;
	}
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RefreshCombatState();
	}
}

float ADBEnemyCharacter::TakeDamage(
    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	// Preserve the opening swing even if the player lands a lethal hit during the approach.
	if (CombatComponent->HasPendingParryLesson() && FMath::IsFinite(Amount))
	{
		Amount = FMath::Min(Amount, HealthComponent->GetHealth() - 1.0f);
	}
	const float Applied = HealthComponent->ApplyDamage(Amount);
	if (Applied <= 0)
	{
		return 0;
	}
	Super::TakeDamage(Applied, Event, EventInstigator, Causer);
	if (HealthComponent->IsDead())
	{
		return Applied;
	}
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		// Every accepted hit reveals its source, including hits absorbed by attack protection.
		AI->RememberAttacker(EventInstigator ? EventInstigator->GetPawn() : Causer);
	}
	if (HasStaggerResistance())
	{
		return Applied;
	}
	const auto* AttackerCombat = Causer ? Causer->FindComponentByClass<UDBCombatComponent>() : nullptr;
	const EDBAttackId Attack = AttackerCombat ? AttackerCombat->GetAttackId() : EDBAttackId::None;
	if (Attack == EDBAttackId::Launcher || GetCharacterMovement()->IsFalling())
	{
		bRecoverAfterLanding = true;
	}
	const bool bForceReaction = Attack == EDBAttackId::Launcher || Attack == EDBAttackId::AirHeavy;
	if (!bForceReaction && GetCharacterMovement()->IsMovingOnGround())
	{
		if (GetWorld()->GetTimeSeconds() < LandingRecoveryEndsAt)
		{
			return Applied;
		}
		if (ReactionInstanceId != INDEX_NONE)
		{
			// Refresh gameplay stun without rewinding a flinch or a successful parry's recoil.
			HitStunEndsAt = GetWorld()->GetTimeSeconds() + GroundHitStunDuration;
			return Applied;
		}
	}
	PlayHitReaction(GetCharacterMovement()->IsFalling() ? AirHitReactionMontage : HitReactionMontage);
	return Applied;
}

void ADBEnemyCharacter::ReactToParry(AActor* Defender)
{
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RememberAttacker(Defender);
	}
	PlayHitReaction(ParryReactionMontage ? ParryReactionMontage : HitReactionMontage);
}

void ADBEnemyCharacter::PlayHitReaction(UAnimMontage* Montage)
{
	CombatComponent->ResetCombat();
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->StopMovement();
	}
	const double FallingSpeed = GetCharacterMovement()->IsFalling() ? GetVelocity().Z : 0.0;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->Velocity.Z = FallingSpeed;
	StopReaction();
	if (GetCharacterMovement()->IsMovingOnGround())
	{
		HitStunEndsAt = GetWorld()->GetTimeSeconds() + GroundHitStunDuration;
	}
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	    Anim && Montage && (GetCharacterMovement()->IsMovingOnGround() || GetCharacterMovement()->IsFalling())
	    && Anim->Montage_Play(Montage) > 0)
	{
		// Keep the authored pose speed. Ground stun may outlast its visual recovery.
		if (GetCharacterMovement()->IsMovingOnGround())
		{
			auto* Instance = Anim->GetActiveInstanceForMontage(Montage);
			ReactionInstanceId = Instance->GetInstanceID();
			Instance->OnMontageEnded.BindUObject(this, &ThisClass::ReactionEnded, ReactionInstanceId);
		}
	}
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RefreshCombatState();
	}
}

void ADBEnemyCharacter::StopReaction()
{
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (auto* Instance = Anim->GetMontageInstanceForID(ReactionInstanceId))
		{
			Instance->OnMontageEnded.Unbind();
			Anim->Montage_Stop(0.08f, Instance->Montage);
		}
		for (UAnimMontage* Visual : {AirHitReactionMontage.Get(), LandingRecoveryMontage.Get()})
		{
			if (Visual)
			{
				Anim->Montage_Stop(0.08f, Visual);
			}
		}
	}
	ReactionInstanceId = INDEX_NONE;
	HitStunEndsAt = 0.0;
}

void ADBEnemyCharacter::ReactionEnded(UAnimMontage*, bool, int32 InstanceId)
{
	if (InstanceId != ReactionInstanceId)
	{
		return;
	}
	ReactionInstanceId = INDEX_NONE;
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RefreshCombatState();
	}
}

void ADBEnemyCharacter::HandleDeath()
{
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->StopBehavior();
	}
	// Finish animation buffer swaps before enabling ragdoll physics on this mesh.
	GetMesh()->HandleExistingParallelEvaluationTask(true, true);
	CombatComponent->ResetCombat();
	StopReaction();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthDisplay->SetVisibility(false);
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetSimulatePhysics(true);
}

void ADBEnemyCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	StopReaction();
	Super::EndPlay(Reason);
}
