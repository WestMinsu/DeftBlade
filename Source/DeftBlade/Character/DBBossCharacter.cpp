#include "Character/DBBossCharacter.h"

#include "AI/DBEnemyAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	constexpr float ThrustRange = 600.0f;
	constexpr float PounceMinimumRange = 570.0f;
	constexpr float PounceRange = 680.0f;
}

ADBBossCharacter::ADBBossCharacter()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	GetCharacterMovement()->RotationRate.Yaw = 210.0;
	for (FName Name : {TEXT("Torso"), TEXT("Legs"), TEXT("Gauntlets"), TEXT("Boots"), TEXT("Helmet")})
	{
		auto* Part = CreateDefaultSubobject<USkeletalMeshComponent>(Name);
		Part->SetupAttachment(GetMesh());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Armour.Add(Part);
	}
}

void ADBBossCharacter::TurnTowards(const FVector& Direction, float DeltaSeconds)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double Yaw = GetActorRotation().Yaw;
	const double Error = FMath::FindDeltaAngleDegrees(Yaw, Direction.Rotation().Yaw);
	if (FMath::Abs(Error) < 1.0)
	{
		return;
	}
	const double MaxStep = GetCharacterMovement()->RotationRate.Yaw * DeltaSeconds;
	// Ease into alignment while limiting how fast a large turn can rotate the grounded body.
	const double Step = FMath::Clamp(Error * (1.0 - FMath::Exp(-8.0 * DeltaSeconds)), -MaxStep, MaxStep);
	SetActorRotation(FRotator(0.0, Yaw + Step, 0.0));
}

void ADBBossCharacter::SharePose()
{
	GetMesh()->SetVisibility(false, false);
	HealthDisplay->SetVisibility(false);
	for (USkeletalMeshComponent* Part : Armour)
	{
		Part->SetLeaderPoseComponent(GetMesh(), true, false);
	}
}

void ADBBossCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SharePose();
}

void ADBBossCharacter::BeginPlay()
{
	Super::BeginPlay();
	SharePose();
	CombatComponent->OnAttackStarted.AddUObject(this, &ThisClass::HandleAttackStarted);
	CombatComponent->OnParried.AddUObject(this, &ThisClass::OpenParryOpening);
}

bool ADBBossCharacter::CanAct() const
{
	// Keep the committed BT task alive during this authored leap, without enabling ordinary airborne attacks.
	return Super::CanAct() || (IsBossLeapAttack(CombatComponent->GetAttackId()) && GetCharacterMovement()->IsFlying());
}

bool ADBBossCharacter::CanBeLaunched() const
{
	return Super::CanBeLaunched() && GetWorld()->GetTimeSeconds() < ParryOpeningEndsAt;
}

bool ADBBossCharacter::HasStaggerResistance() const
{
	// An earned aerial chain may finish, but it cannot grant another launch after landing.
	const bool bFinishingAirCombo = bAirComboActive && GetCharacterMovement()->IsFalling();
	return Super::HasStaggerResistance() || (!CanBeLaunched() && !bFinishingAirCombo);
}

void ADBBossCharacter::OpenParryOpening(AActor*)
{
	ParryOpeningEndsAt = GetWorld()->GetTimeSeconds() + ParryOpeningDuration;
}

void ADBBossCharacter::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (bAirComboActive && GetCharacterMovement()->IsMovingOnGround())
	{
		bAirComboActive = false;
		ParryOpeningEndsAt = 0.0;
	}
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void ADBBossCharacter::HandleAttackStarted()
{
	ParryOpeningEndsAt = 0.0;
	PreviousAttack = CombatComponent->GetAttackId();
	if (IsBossLeapAttack(PreviousAttack))
	{
		// Flying lets CMC apply the montage's authored vertical root motion, including collision sweeps.
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		SetActorTickEnabled(true);
	}
}

void ADBBossCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsBossLeapAttack(CombatComponent->GetAttackId()))
	{
		if (GetCharacterMovement()->IsFlying())
		{
			// Completion, parry and interruption all return to gravity. Death's disabled movement stays disabled.
			GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		}
		SetActorTickEnabled(false);
	}
}

void ADBBossCharacter::Activate(APawn* Player)
{
	if (bActive || !Player || HealthComponent->IsDead())
	{
		return;
	}
	bActive = true;
	SpawnDefaultController();
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RememberAttacker(Player);
	}
}

float ADBBossCharacter::TakeDamage(
    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	if (!bActive)
	{
		return 0.0f;
	}
	const auto* Attacker = Causer ? Causer->FindComponentByClass<UDBCombatComponent>() : nullptr;
	const bool bAcceptsLauncher =
	    Attacker && Attacker->GetAttackId() == EDBAttackId::Launcher && CanBeLaunched() && !HasStaggerResistance();
	const float Applied = Super::TakeDamage(Amount, Event, EventInstigator, Causer);
	if (Applied > 0.0f && !HealthComponent->IsDead() && bAcceptsLauncher)
	{
		bAirComboActive = true;
		ParryOpeningEndsAt = 0.0;
	}
	return Applied;
}

bool ADBBossCharacter::CanAttackAtDistance(float Distance) const
{
	return Super::CanAttackAtDistance(Distance) || SelectApproachAttack(Distance) != EDBAttackId::None;
}

EDBAttackId ADBBossCharacter::SelectApproachAttack(float Distance) const
{
	if (Distance >= PounceMinimumRange && Distance <= PounceRange && PreviousAttack != EDBAttackId::BossPounce)
	{
		return EDBAttackId::BossPounce;
	}
	if (Distance <= ThrustRange && PreviousAttack != EDBAttackId::BossThrust)
	{
		return EDBAttackId::BossThrust;
	}
	return EDBAttackId::None;
}

EDBAttackId ADBBossCharacter::SelectAttack(float Distance, bool bCombo) const
{
	if (!Super::CanAttackAtDistance(Distance))
	{
		return SelectApproachAttack(Distance);
	}

	const auto* AI = Cast<ADBEnemyAIController>(GetController());
	const auto* Target = AI && AI->GetBlackboardComponent()
	                         ? Cast<ACharacter>(AI->GetBlackboardComponent()->GetValueAsObject(DBEnemyBB::Target))
	                         : nullptr;
	const bool bTargetAirborne = Target && Target->GetCharacterMovement()->IsFalling();
	EDBAttackId Next;
	if (bTargetAirborne && Distance <= 280.0f)
	{
		Next = PreviousAttack == EDBAttackId::Light1 ? EDBAttackId::BossLeap : EDBAttackId::Light1;
	}
	else if (Distance > 280.0f)
	{
		Next = PreviousAttack == EDBAttackId::BossLeap ? EDBAttackId::Heavy2 : EDBAttackId::BossLeap;
	}
	else
	{
		Next = bCombo
		           ? (PreviousAttack == EDBAttackId::BossWhirlwind ? EDBAttackId::Heavy1 : EDBAttackId::BossWhirlwind)
		           : EDBAttackId::Light1;
	}
	if (Next == PreviousAttack)
	{
		Next = Next == EDBAttackId::Heavy1 ? EDBAttackId::Light1 : EDBAttackId::Heavy1;
	}
	return Next;
}
