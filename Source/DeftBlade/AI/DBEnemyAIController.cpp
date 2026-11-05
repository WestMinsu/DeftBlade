#include "AI/DBEnemyAIController.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Character/DBEnemyCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"

ADBEnemyAIController::ADBEnemyAIController()
{
	SetGenericTeamId(FGenericTeamId(1));
	auto* Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Senses);
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
	SightConfig->SightRadius = 1400;
	SightConfig->LoseSightRadius = 1700;
	SightConfig->PeripheralVisionAngleDegrees = 80;
	// Keep nearby jumps inside the sight cone without bypassing line-of-sight checks.
	SightConfig->PointOfViewBackwardOffset = 100;
	SightConfig->NearClippingRadius = 100;
	SightConfig->SetMaxAge(5);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
	Senses->ConfigureSense(*SightConfig);
	Senses->SetDominantSense(SightConfig->GetSenseImplementation());
}

ETeamAttitude::Type ADBEnemyAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	if (Other.IsA<ADBPlayerCharacter>())
	{
		return ETeamAttitude::Hostile;
	}
	return Other.IsA<ADBEnemyCharacter>() ? ETeamAttitude::Friendly : ETeamAttitude::Neutral;
}

void ADBEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	GetPerceptionComponent()->SetSenseEnabled(SightConfig->GetSenseImplementation(), true);
	GetPerceptionComponent()->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ThisClass::PerceptionUpdated);
	if (const auto* Enemy = Cast<ADBEnemyCharacter>(InPawn); Enemy && Enemy->GetBehaviorTree())
	{
		RunBehaviorTree(Enemy->GetBehaviorTree());
		RefreshCombatState();
	}
}

void ADBEnemyAIController::ForgetTarget()
{
	if (TrackedTarget.IsValid())
	{
		if (auto* Health = TrackedTarget->FindComponentByClass<UDBHealthComponent>())
		{
			Health->OnDeath.RemoveAll(this);
		}
	}
	TrackedTarget.Reset();
	ClearFocus(EAIFocusPriority::Gameplay);
	bHasSight = false;
	LastSeenAt = -SearchDuration;
}

void ADBEnemyAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	auto* Enemy = Cast<ADBEnemyCharacter>(GetPawn());
	const auto* Combat = Enemy ? Enemy->FindComponentByClass<UDBCombatComponent>() : nullptr;
	// The attack task owns aiming once its Blackboard branch is eligible.
	if (bHasSight && TrackedTarget.IsValid() && Blackboard && !Blackboard->GetValueAsBool(DBEnemyBB::CanAttack) && Enemy
	    && Enemy->CanAct() && Combat && Combat->GetActionState() == EDBActionState::None
	    && GetMoveStatus() == EPathFollowingStatus::Idle && Enemy->GetVelocity().SizeSquared2D() < 100.)
	{
		const FRotator Facing(0, (TrackedTarget->GetActorLocation() - Enemy->GetActorLocation()).Rotation().Yaw, 0);
		Enemy->SetActorRotation(FMath::RInterpConstantTo(Enemy->GetActorRotation(), Facing, DeltaSeconds, 180.f));
	}
}

void ADBEnemyAIController::PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!IsValid(Actor) || !Actor->IsA<ADBPlayerCharacter>())
	{
		return;
	}
	const auto* Health = Actor->FindComponentByClass<UDBHealthComponent>();
	if (!Health || Health->IsDead())
	{
		return;
	}
	if (Stimulus.WasSuccessfullySensed())
	{
		if (TrackedTarget.Get() != Actor)
		{
			ForgetTarget();
			TrackedTarget = Actor;
			Actor->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::ForgetTarget);
		}
		bHasSight = true;
	}
	else if (TrackedTarget.Get() == Actor)
	{
		bHasSight = false;
	}
	RefreshCombatState();
}

void ADBEnemyAIController::RememberAttacker(AActor* Attacker)
{
	if (!IsValid(Attacker) || !Attacker->IsA<ADBPlayerCharacter>())
	{
		return;
	}
	if (TrackedTarget.Get() != Attacker)
	{
		ForgetTarget();
		TrackedTarget = Attacker;
		Attacker->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::ForgetTarget);
	}
	LastSeenLocation = Attacker->GetActorLocation();
	LastSeenAt = GetWorld()->GetTimeSeconds();
	// Keep existing sight. A newly remembered attacker still needs the sight sense to acquire it.
	if (GetPawn())
	{
		const FVector Direction = LastSeenLocation - GetPawn()->GetActorLocation();
		GetPawn()->SetActorRotation(FRotator(0, Direction.Rotation().Yaw, 0));
	}
	GetPerceptionComponent()->RequestStimuliListenerUpdate();
}

void ADBEnemyAIController::RefreshCombatState()
{
	if (!Blackboard)
	{
		return;
	}
	const auto* Enemy = Cast<ADBEnemyCharacter>(GetPawn());
	const double Now = GetWorld()->GetTimeSeconds();
	if (!TrackedTarget.IsValid() || TrackedTarget->FindComponentByClass<UDBHealthComponent>()->IsDead())
	{
		ForgetTarget();
	}
	if (bHasSight && TrackedTarget.IsValid())
	{
		LastSeenLocation = TrackedTarget->GetActorLocation();
		LastSeenAt = Now;
	}
	if (TrackedTarget.IsValid())
	{
		// Keep perception toward the known position while the body follows its path around other enemies.
		// A location focus cannot track an unseen player's later movement.
		SetFocalPoint(LastSeenLocation);
	}
	const bool bCanAct = Enemy && Enemy->CanAct();
	const auto* Combat = Enemy ? Enemy->FindComponentByClass<UDBCombatComponent>() : nullptr;
	const bool bCommittedAttack = Combat && Combat->IsAttacking();
	AActor* Target = bHasSight || bCommittedAttack ? TrackedTarget.Get() : nullptr;
	const bool bInRange = Target && Enemy
	                      && FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
	                             <= FMath::Square(Enemy->GetAttackRange());
	FVector MoveGoal = LastSeenLocation;
	if (Enemy && TrackedTarget.IsValid())
	{
		if (auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation GroundGoal;
			// Default projection height does not reach the floor during a double jump.
			if (Navigation->ProjectPointToNavigation(
			        LastSeenLocation, GroundGoal, FVector(50, 50, 1000), &Enemy->GetNavAgentPropertiesRef()))
			{
				MoveGoal = GroundGoal.Location;
			}
		}
	}
	// Activating a BT branch can start MoveTo immediately; publish its goal first.
	Blackboard->SetValueAsVector(DBEnemyBB::LastKnownLocation, MoveGoal);
	Blackboard->SetValueAsBool(DBEnemyBB::CanAct, bCanAct);
	Blackboard->SetValueAsObject(DBEnemyBB::Target, Target);
	Blackboard->SetValueAsBool(
	    DBEnemyBB::HasLastKnownLocation, TrackedTarget.IsValid() && Now - LastSeenAt < SearchDuration);
	// Finish the chosen pattern when it misses or leaves sight. Death and interruption still abort it.
	Blackboard->SetValueAsBool(DBEnemyBB::CanAttack,
	    bCanAct && Target && Combat && (bCommittedAttack || (bHasSight && bInRange && CanStartAttack())));
	if (TrackedTarget.IsValid() && !bHasSight && Now - LastSeenAt >= SearchDuration)
	{
		ForgetTarget();
	}
}

bool ADBEnemyAIController::CanStartAttack() const
{
	if (!TrackedTarget.IsValid() || !bHasSight || GetWorld()->GetTimeSeconds() < NextAttackAt)
	{
		return false;
	}
	for (TActorIterator<ADBEnemyAIController> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->TrackedTarget != TrackedTarget)
		{
			continue;
		}
		if (It->HasCommittedAttack())
		{
			return false;
		}
	}
	return true;
}

bool ADBEnemyAIController::HasCommittedAttack() const
{
	const auto* Enemy = Cast<ADBEnemyCharacter>(GetPawn());
	const auto* Combat = Enemy ? Enemy->FindComponentByClass<UDBCombatComponent>() : nullptr;
	if (!Combat)
	{
		return false;
	}
	const auto Pending = Combat->GetBufferedAction().Input;
	if (Pending == EDBActionInput::LightAttack || Pending == EDBActionInput::HeavyAttack)
	{
		return true;
	}
	if (!Combat->IsAttacking())
	{
		return false;
	}
	const auto* Attack = Combat->FindAttack(Combat->GetAttackId());
	const auto* Anim = Enemy->GetMesh()->GetAnimInstance();
	const auto* Instance = Attack && Anim ? Anim->GetInstanceForMontage(Attack->Montage) : nullptr;
	return !Instance
	       || Instance->GetPosition()
	              < Attack->HitWindows.Last().End + GroupAttackRecovery * Attack->PlayRate * Attack->Montage->RateScale;
}

void ADBEnemyAIController::AttackFinished()
{
	NextAttackAt = GetWorld()->GetTimeSeconds() + AttackCooldown;
	RefreshCombatState();
}

void ADBEnemyAIController::StopBehavior()
{
	StopMovement();
	ForgetTarget();
	if (Blackboard)
	{
		Blackboard->ClearValue(DBEnemyBB::Target);
		Blackboard->SetValueAsBool(DBEnemyBB::CanAct, false);
		Blackboard->SetValueAsBool(DBEnemyBB::CanAttack, false);
		Blackboard->SetValueAsBool(DBEnemyBB::HasLastKnownLocation, false);
	}
	if (BrainComponent)
	{
		BrainComponent->StopLogic(TEXT("Enemy stopped"));
	}
	GetPerceptionComponent()->SetSenseEnabled(SightConfig->GetSenseImplementation(), false);
}

void ADBEnemyAIController::OnUnPossess()
{
	StopBehavior();
	GetPerceptionComponent()->OnTargetPerceptionUpdated.RemoveAll(this);
	NextAttackAt = 0;
	Super::OnUnPossess();
}
