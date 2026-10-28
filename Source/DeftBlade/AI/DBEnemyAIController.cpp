#include "AI/DBEnemyAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Character/DBEnemyCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
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
	bHasSight = false;
	LastSeenAt = -SearchDuration;
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
	const bool bCanAct = Enemy && Enemy->CanAct();
	const auto* Combat = Enemy ? Enemy->FindComponentByClass<UDBCombatComponent>() : nullptr;
	const bool bCommittedAttack = Combat && Combat->IsAttacking();
	AActor* Target = bHasSight || bCommittedAttack ? TrackedTarget.Get() : nullptr;
	const bool bInRange = Target && Enemy
	                      && FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
	                             <= FMath::Square(Enemy->GetAttackRange())
	                      && FMath::Abs(Enemy->GetActorLocation().Z - Target->GetActorLocation().Z) < 100;
	Blackboard->SetValueAsBool(DBEnemyBB::CanAct, bCanAct);
	Blackboard->SetValueAsObject(DBEnemyBB::Target, Target);
	Blackboard->SetValueAsVector(DBEnemyBB::LastKnownLocation, LastSeenLocation);
	Blackboard->SetValueAsBool(
	    DBEnemyBB::HasLastKnownLocation, TrackedTarget.IsValid() && Now - LastSeenAt < SearchDuration);
	// Finish the chosen pattern when it misses or leaves sight. Death and interruption still abort it.
	Blackboard->SetValueAsBool(DBEnemyBB::CanAttack,
	    bCanAct && Target && Combat && (bCommittedAttack || (bHasSight && bInRange && Now >= NextAttackAt)));
	if (TrackedTarget.IsValid() && !bHasSight && Now - LastSeenAt >= SearchDuration)
	{
		ForgetTarget();
	}
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
