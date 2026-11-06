#include "AI/BTTask_DBPatrol.h"

#include "AI/DBEnemyAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/DBEnemyCharacter.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTTask_DBPatrol::UBTTask_DBPatrol()
{
	NodeName = TEXT("Patrol route");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;
	BlackboardKey.SelectedKeyName = DBEnemyBB::PatrolLocation;
	AcceptableRadius = 50.f;
	bReachTestIncludesAgentRadius = false;
	bAllowPartialPath = false;
}

EBTNodeResult::Type UBTTask_DBPatrol::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	auto* AI = OwnerComp.GetAIOwner();
	auto* Enemy = AI ? Cast<ADBEnemyCharacter>(AI->GetPawn()) : nullptr;
	auto* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Enemy || !Enemy->CanAct() || !Blackboard)
	{
		return EBTNodeResult::Failed;
	}
	const auto& Points = Enemy->GetPatrolPoints();
	for (int32 Checked = 0; Checked < Points.Num(); ++Checked)
	{
		PointIndex %= Points.Num();
		if (IsValid(Points[PointIndex]))
		{
			Blackboard->SetValueAsVector(BlackboardKey.SelectedKeyName, Points[PointIndex]->GetActorLocation());
			Movement = Enemy->GetCharacterMovement();
			PreviousSpeed = Movement->MaxWalkSpeed;
			Movement->MaxWalkSpeed = FMath::Min(PreviousSpeed, Enemy->GetPatrolSpeed());
			return Super::ExecuteTask(OwnerComp, NodeMemory);
		}
		++PointIndex;
	}
	return EBTNodeResult::Failed;
}

void UBTTask_DBPatrol::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result)
{
	Super::OnTaskFinished(OwnerComp, NodeMemory, Result);
	if (Movement.IsValid())
	{
		Movement->MaxWalkSpeed = PreviousSpeed;
		if (Result != EBTNodeResult::Aborted)
		{
			++PointIndex;
		}
	}
	Movement.Reset();
}
