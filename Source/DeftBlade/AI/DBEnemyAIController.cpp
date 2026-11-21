#include "AI/DBEnemyAIController.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Character/DBBossCharacter.h"
#include "Character/DBEnemyCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBTargetingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
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
	if (InPawn->IsA<ADBBossCharacter>())
	{
		GetPathFollowingComponent()->PostProcessMove.BindUObject(this, &ThisClass::LimitBossMovementWhileTurning);
	}
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
	bHasClosePressure = false;
	ReconsiderPressureAt = PressureHeldUntil = HitPressureUntil = 0.0;
	ClearFocus(EAIFocusPriority::Gameplay);
	bHasSight = false;
	SearchStartedAt = -1;
}

void ADBEnemyAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	auto* Enemy = Cast<ADBEnemyCharacter>(GetPawn());
	const auto* Combat = Enemy ? Enemy->FindComponentByClass<UDBCombatComponent>() : nullptr;
	const bool bCanFaceTarget = TrackedTarget.IsValid() && Blackboard && Enemy && Enemy->CanAct() && Combat
	                            && Combat->GetActionState() == EDBActionState::None;
	if (auto* Boss = Cast<ADBBossCharacter>(Enemy); Boss && bCanFaceTarget && bHasSight)
	{
		Boss->GetCharacterMovement()->bOrientRotationToMovement = false;
		// Keep the same facing update during pursuit, braking and pre-attack aim.
		const FVector Direction = TrackedTarget->GetActorLocation() - Boss->GetActorLocation();
		Boss->TurnTowards(Direction.GetSafeNormal2D(), DeltaSeconds);
		return;
	}

	bool bBackingAway = false;
	if (bCanFaceTarget && bHasSight && !Blackboard->GetValueAsBool(DBEnemyBB::CanAttack))
	{
		const FVector TowardsTarget = (LastSeenLocation - Enemy->GetActorLocation()).GetSafeNormal2D();
		const FVector GoalDirection =
		    (Blackboard->GetValueAsVector(DBEnemyBB::CombatLocation) - Enemy->GetActorLocation()).GetSafeNormal2D();
		// Keep facing through the final braking frames, after MoveTo has reached its goal.
		bBackingAway = (GetMoveStatus() == EPathFollowingStatus::Moving
		                   && FVector::DotProduct(GoalDirection, TowardsTarget) < -.1f)
		               || (Enemy->GetVelocity().SizeSquared2D() > 100.
		                   && FVector::DotProduct(Enemy->GetVelocity().GetSafeNormal2D(), TowardsTarget) < -.1f);
	}
	if (Enemy)
	{
		Enemy->GetCharacterMovement()->bOrientRotationToMovement = !bBackingAway;
	}
	// The attack task owns aiming once its Blackboard branch is eligible.
	if (bCanFaceTarget && !Blackboard->GetValueAsBool(DBEnemyBB::CanAttack)
	    && (bBackingAway
	        || (GetMoveStatus() == EPathFollowingStatus::Idle && Enemy->GetVelocity().SizeSquared2D() < 100.)))
	{
		const FRotator Facing(0, (LastSeenLocation - Enemy->GetActorLocation()).Rotation().Yaw, 0);
		Enemy->SetActorRotation(FMath::RInterpConstantTo(Enemy->GetActorRotation(), Facing, DeltaSeconds, 180.f));
	}
}

void ADBEnemyAIController::LimitBossMovementWhileTurning(UPathFollowingComponent*, FVector& MoveInput)
{
	const auto* Boss = Cast<ADBBossCharacter>(GetPawn());
	if (!Boss || !Boss->CanAct() || !bHasSight || !TrackedTarget.IsValid())
	{
		return;
	}

	const FVector TowardsTarget = (TrackedTarget->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();
	if (!TowardsTarget.IsNearlyZero())
	{
		const double Alignment = FVector::DotProduct(Boss->GetActorForwardVector(), TowardsTarget);
		// Reduce path acceleration input from 45 to 100 degrees of yaw error, then keep a slow step.
		MoveInput *= FMath::GetMappedRangeValueClamped(FVector2D(-0.174, 0.707), FVector2D(0.20, 1.0), Alignment);
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
		SearchStartedAt = -1;
	}
	RefreshCombatState();
}

void ADBEnemyAIController::RememberAttacker(AActor* Attacker)
{
	if (!IsValid(Attacker) || !Attacker->IsA<ADBPlayerCharacter>())
	{
		return;
	}
	auto* Health = Attacker->FindComponentByClass<UDBHealthComponent>();
	if (!Health || Health->IsDead())
	{
		return;
	}
	if (TrackedTarget.Get() != Attacker)
	{
		ForgetTarget();
		TrackedTarget = Attacker;
		Health->OnDeath.AddUObject(this, &ThisClass::ForgetTarget);
	}
	LastSeenLocation = Attacker->GetActorLocation();
	HitPressureUntil = GetWorld()->GetTimeSeconds() + 1.0;
	ReconsiderPressureAt = 0.0;
	SearchStartedAt = -1;
	// Keep existing sight. A newly remembered attacker still needs the sight sense to acquire it.
	GetPerceptionComponent()->RequestStimuliListenerUpdate();
	RefreshCombatState();
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
	if (TrackedTarget.IsValid() && !bHasSight && bCanAct && !bCommittedAttack
	    && GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		if (SearchStartedAt < 0)
		{
			SearchStartedAt = Now;
		}
		if (Now - SearchStartedAt >= SearchDuration)
		{
			ForgetTarget();
		}
	}
	else
	{
		SearchStartedAt = -1;
	}
	AActor* Target = bHasSight || bCommittedAttack ? TrackedTarget.Get() : nullptr;
	UpdateClosePressure();
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
	Blackboard->SetValueAsVector(
	    DBEnemyBB::CombatLocation, Enemy && bHasSight ? GetCombatLocation(MoveGoal) : MoveGoal);
	Blackboard->SetValueAsBool(DBEnemyBB::CanAct, bCanAct);
	Blackboard->SetValueAsObject(DBEnemyBB::Target, Target);
	Blackboard->SetValueAsBool(DBEnemyBB::HasLastKnownLocation, TrackedTarget.IsValid());
	// Finish the chosen pattern when it misses or leaves sight. Death and interruption still abort it.
	Blackboard->SetValueAsBool(DBEnemyBB::CanAttack,
	    bCanAct && Target && Combat && (bCommittedAttack || (bHasSight && bInRange && CanStartAttack())));
}

bool ADBEnemyAIController::CanStartAttack() const
{
	if (!TrackedTarget.IsValid() || !bHasSight || !bHasClosePressure || GetWorld()->GetTimeSeconds() < NextAttackAt)
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

void ADBEnemyAIController::UpdateClosePressure()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!TrackedTarget.IsValid() || Now < ReconsiderPressureAt)
	{
		return;
	}
	ReconsiderPressureAt = Now + .35;
	struct FCandidate
	{
		ADBEnemyAIController* AI;
		int32 Priority;
		float Score;
	};
	TArray<FCandidate> Candidates;
	const auto* Player = Cast<APawn>(TrackedTarget.Get());
	const auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	const auto* Targeting = Player ? Player->FindComponentByClass<UDBTargetingComponent>() : nullptr;
	int32 Width = 0, Height = 0;
	if (PC)
	{
		PC->GetViewportSize(Width, Height);
	}
	for (TActorIterator<ADBEnemyAIController> It(GetWorld()); It; ++It)
	{
		ADBEnemyAIController* AI = *It;
		if (AI->TrackedTarget != TrackedTarget)
		{
			continue;
		}
		auto* Enemy = Cast<ADBEnemyCharacter>(AI->GetPawn());
		if (!Enemy || Enemy->FindComponentByClass<UDBHealthComponent>()->IsDead())
		{
			continue;
		}
		const auto* Combat = Enemy->FindComponentByClass<UDBCombatComponent>();
		const bool bAttacking = Combat && (Combat->IsAttacking() || AI->HasCommittedAttack());
		const bool bEngaged = Now < AI->HitPressureUntil || (AI->bHasClosePressure && !Enemy->CanAct());
		if (!AI->bHasSight && !bAttacking && !bEngaged)
		{
			AI->bHasClosePressure = false;
			continue;
		}
		const int32 Priority = bAttacking                                             ? 3
		                       : bEngaged                                             ? 2
		                       : AI->bHasClosePressure && Now < AI->PressureHeldUntil ? 1
		                                                                              : 0;
		// Use this enemy's knowledge, never an unseen target's current position.
		float Score = -FVector::Dist2D(Enemy->GetActorLocation(), AI->LastSeenLocation);
		FVector2D Screen;
		if (PC && Width > 0 && Height > 0 && PC->ProjectWorldLocationToScreen(Enemy->GetActorLocation(), Screen)
		    && Screen.X >= 0 && Screen.Y >= 0 && Screen.X <= Width && Screen.Y <= Height)
		{
			Score += 180.0f;
		}
		if (Targeting && Targeting->GetLockedTarget() == Enemy)
		{
			Score += 250.0f;
		}
		if (AI->bHasClosePressure)
		{
			Score += 60.0f;
		}
		Candidates.Add({AI, Priority, Score});
	}
	Candidates.Sort(
	    [](const FCandidate& A, const FCandidate& B)
	    {
		    if (A.Priority != B.Priority)
		    {
			    return A.Priority > B.Priority;
		    }
		    return A.Score == B.Score ? A.AI->GetUniqueID() < B.AI->GetUniqueID() : A.Score > B.Score;
	    });
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		ADBEnemyAIController* AI = Candidates[Index].AI;
		const bool bSelected = Index < 2;
		if (bSelected && !AI->bHasClosePressure)
		{
			AI->PressureHeldUntil = Now + 1.25;
		}
		AI->bHasClosePressure = bSelected;
		// Any group member may refresh the group, but they share one short evaluation interval.
		AI->ReconsiderPressureAt = Now + .35;
	}
}

FVector ADBEnemyAIController::GetCombatLocation(const FVector& GroundTarget) const
{
	const auto* Enemy = CastChecked<ADBEnemyCharacter>(GetPawn());
	const FVector Position = Enemy->GetActorLocation();
	const auto* Player = Cast<ACharacter>(TrackedTarget.Get());
	const float BodyGap = Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius()
	                      + (Player ? Player->GetCapsuleComponent()->GetScaledCapsuleRadius() : 34.0f) + 16.0f;
	// Short fist reach limits the body gap; never stop outside the authored attack range.
	const float NearDistance =
	    FMath::Max(FMath::Min(BodyGap, Enemy->GetAttackRange() - 10.0f), Enemy->GetAttackRange() - 50.0f);
	const float Distance = bHasClosePressure ? NearDistance : FMath::Max(OuterDistance, NearDistance + 180.0f);
	FVector Direction = (Position - GroundTarget).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = -Enemy->GetActorForwardVector();
	}
	TArray<FVector> Neighbors;
	for (TActorIterator<ADBEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Enemy && !It->FindComponentByClass<UDBHealthComponent>()->IsDead())
		{
			Neighbors.Add(It->GetActorLocation());
		}
	}
	auto HasSpace = [&](const FVector& Point)
	{
		return !Neighbors.ContainsByPredicate(
		    [&](const FVector& Other)
		    {
			    return FVector::DistSquared2D(Point, Other) < FMath::Square(EnemySeparation);
		    });
	};
	auto FitsZone = [&](const FVector& Point, bool bCurrentPosition = false)
	{
		const FVector Offset = Point - GroundTarget;
		const float Maximum = bHasClosePressure ? FMath::Min(Distance + 30.0f,
		                                              Enemy->GetAttackRange() - (bCurrentPosition ? 0.0f : 10.0f))
		                                        : Distance + 30.0f;
		return Offset.Size2D() >= Distance - 30.0f && Offset.Size2D() <= Maximum
		       && FVector::DotProduct(Offset.GetSafeNormal2D(), Direction) >= .2f && HasSpace(Point);
	};
	// A valid position is enough. Preserve it instead of orbiting toward a new optimum every update.
	if (FitsZone(Position, true))
	{
		return Position;
	}
	if (Blackboard->IsVectorValueSet(DBEnemyBB::CombatLocation))
	{
		const FVector Previous = Blackboard->GetValueAsVector(DBEnemyBB::CombatLocation);
		if (FitsZone(Previous))
		{
			return Previous;
		}
	}
	if (auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		const ANavigationData* NavData = Navigation->GetNavDataForProps(Enemy->GetNavAgentPropertiesRef(), Position);
		for (float Angle : {0.0f, 25.0f, -25.0f, 50.0f, -50.0f, 75.0f, -75.0f})
		{
			const FVector Desired = GroundTarget + Direction.RotateAngleAxis(Angle, FVector::UpVector) * Distance;
			FNavLocation Projected;
			if (NavData && Navigation->ProjectPointToNavigation(Desired, Projected, FVector(60, 60, 150), NavData)
			    && FitsZone(Projected.Location)
			    && Navigation->TestPathSync(FPathFindingQuery(this, *NavData, Position, Projected.Location)))
			{
				return Projected.Location;
			}
		}
	}
	// A narrow or blocked area must not produce an unreachable goal or a path through the player.
	return Position;
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
	if (auto* Enemy = Cast<ADBEnemyCharacter>(GetPawn()))
	{
		Enemy->GetCharacterMovement()->bOrientRotationToMovement = true;
	}
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
	auto& PostProcessMove = GetPathFollowingComponent()->PostProcessMove;
	if (PostProcessMove.IsBoundToObject(this))
	{
		PostProcessMove.Unbind();
	}
	GetPerceptionComponent()->OnTargetPerceptionUpdated.RemoveAll(this);
	NextAttackAt = 0;
	Super::OnUnPossess();
}
