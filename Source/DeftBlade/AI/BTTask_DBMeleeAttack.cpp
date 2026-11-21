#include "AI/BTTask_DBMeleeAttack.h"
#include "AI/DBEnemyAIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/DBBossCharacter.h"
#include "Character/DBEnemyCharacter.h"
#include "Combat/DBCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

UBTTask_DBMeleeAttack::UBTTask_DBMeleeAttack()
{
	NodeName = TEXT("Aim and commit melee attack");
	// Each agent owns its request ID. Asset template nodes must never hold mutable agent state.
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTTask_DBMeleeAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8*)
{
	RequestId = 0;
	bStarted = false;
	Combat.Reset();
	auto* AI = Cast<ADBEnemyAIController>(OwnerComp.GetAIOwner());
	auto* Enemy = AI ? Cast<ADBEnemyCharacter>(AI->GetPawn()) : nullptr;
	AActor* Target = AI && AI->GetBlackboardComponent()
	                     ? Cast<AActor>(AI->GetBlackboardComponent()->GetValueAsObject(DBEnemyBB::Target))
	                     : nullptr;
	if (!Enemy || !Enemy->CanAct() || !IsValid(Target)
	    || FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
	           > FMath::Square(Enemy->GetAttackRange()))
	{
		return EBTNodeResult::Failed;
	}
	Combat = Enemy->FindComponentByClass<UDBCombatComponent>();
	if (Combat->GetActionState() != EDBActionState::None)
	{
		return EBTNodeResult::Failed;
	}
	AI->StopMovement();
	AttackId = Enemy->SelectAttack(
	    FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation()), FMath::FRand() < ComboChance);
	const FDBAttackDefinition* Attack = Combat->FindAttack(AttackId);
	if (!Attack || !Attack->IsValid())
	{
		return EBTNodeResult::Failed;
	}
	AimStartedAt = Enemy->GetWorld()->GetTimeSeconds();
	return EBTNodeResult::InProgress;
}

void UBTTask_DBMeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8*, float DeltaSeconds)
{
	const auto* AI = Cast<ADBEnemyAIController>(OwnerComp.GetAIOwner());
	auto* Enemy = AI ? Cast<ADBEnemyCharacter>(AI->GetPawn()) : nullptr;
	const AActor* Target = AI && AI->GetBlackboardComponent()
	                           ? Cast<AActor>(AI->GetBlackboardComponent()->GetValueAsObject(DBEnemyBB::Target))
	                           : nullptr;
	if (!Combat.IsValid() || !Enemy || !Enemy->CanAct() || !IsValid(Target))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	const FRotator DesiredRotation(0, (Target->GetActorLocation() - Enemy->GetActorLocation()).Rotation().Yaw, 0);
	auto TurnTowardsTarget = [&]()
	{
		Enemy->SetActorRotation(
		    FMath::RInterpConstantTo(Enemy->GetActorRotation(), DesiredRotation, DeltaSeconds, 360.f));
	};
	if (RequestId == 0)
	{
		if (FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
		    > FMath::Square(Enemy->GetAttackRange()))
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
		const bool bBoss = Enemy->IsA<ADBBossCharacter>();
		if (!bBoss)
		{
			TurnTowardsTarget();
		}
		if (!AI->CanStartAttack())
		{
			// Waiting for an ally is not a failed aim and must not restart this agent's cooldown.
			AimStartedAt = Enemy->GetWorld()->GetTimeSeconds();
			return;
		}
		const double AimTimeLimit = bBoss ? 1.10 : 0.75;
		if (Enemy->GetWorld()->GetTimeSeconds() - AimStartedAt > AimTimeLimit)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Enemy->GetActorRotation().Yaw, DesiredRotation.Yaw)) <= 15.0)
		{
			Combat->RequestAttack(AttackId);
			RequestId = Combat->GetBufferedAction().Id;
			if (!RequestId)
			{
				FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			}
		}
		return;
	}
	if (Combat->GetActiveActionId() == RequestId)
	{
		bStarted = true;
		const FDBAttackDefinition* Attack = Combat->FindAttack(Combat->GetAttackId());
		UAnimInstance* Anim = Enemy->GetMesh()->GetAnimInstance();
		// Track during the visible preparation, then commit the whole pattern before its first hit.
		if (Attack && Anim
		    && Anim->Montage_GetPosition(Attack->Montage)
		           < Attack->HitWindows[0].Start - 0.10f * Attack->PlayRate * Attack->Montage->RateScale)
		{
			TurnTowardsTarget();
		}
		return;
	}
	if (Combat->GetBufferedAction().Id == RequestId)
	{
		return;
	}
	FinishLatentTask(OwnerComp, bStarted ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
}

void UBTTask_DBMeleeAttack::CancelOwnedAttack()
{
	if (Combat.IsValid() && RequestId)
	{
		if (Combat->GetBufferedAction().Id == RequestId)
		{
			Combat->ResetCombat();
		}
		else
		{
			Combat->FinishAction(RequestId);
		}
	}
}

void UBTTask_DBMeleeAttack::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* Memory, EBTNodeResult::Type Result)
{
	CancelOwnedAttack();
	RequestId = 0;
	Combat.Reset();
	if (auto* AI = Cast<ADBEnemyAIController>(OwnerComp.GetAIOwner());
	    AI && (bStarted || Result != EBTNodeResult::Aborted))
	{
		AI->AttackFinished();
	}
	Super::OnTaskFinished(OwnerComp, Memory, Result);
}
