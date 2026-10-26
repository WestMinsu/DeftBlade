#include "AI/BTTask_DBMeleeAttack.h"
#include "AI/DBEnemyAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/DBEnemyCharacter.h"
#include "Combat/DBCombatComponent.h"

UBTTask_DBMeleeAttack::UBTTask_DBMeleeAttack()
{
	NodeName = TEXT("Committed melee attack");
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
	const FVector Direction = Target->GetActorLocation() - Enemy->GetActorLocation();
	Enemy->SetActorRotation(FRotator(0, Direction.Rotation().Yaw, 0));
	Combat->RequestAttack(false);
	RequestId = Combat->GetBufferedAction().Id;
	return RequestId ? EBTNodeResult::InProgress : EBTNodeResult::Failed;
}

void UBTTask_DBMeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8*, float)
{
	const auto* AI = Cast<ADBEnemyAIController>(OwnerComp.GetAIOwner());
	const auto* Enemy = AI ? Cast<ADBEnemyCharacter>(AI->GetPawn()) : nullptr;
	if (!Combat.IsValid() || !Enemy || !Enemy->CanAct())
	{
		CancelOwnedAttack();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	if (Combat->GetActiveActionId() == RequestId)
	{
		bStarted = true;
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

EBTNodeResult::Type UBTTask_DBMeleeAttack::AbortTask(UBehaviorTreeComponent&, uint8*)
{
	CancelOwnedAttack();
	return EBTNodeResult::Aborted;
}

void UBTTask_DBMeleeAttack::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* Memory, EBTNodeResult::Type Result)
{
	CancelOwnedAttack();
	RequestId = 0;
	Combat.Reset();
	if (auto* AI = Cast<ADBEnemyAIController>(OwnerComp.GetAIOwner()))
	{
		AI->AttackFinished();
	}
	Super::OnTaskFinished(OwnerComp, Memory, Result);
}
