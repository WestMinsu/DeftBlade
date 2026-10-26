#include "AI/BTService_DBUpdateCombat.h"
#include "AI/DBEnemyAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTService_DBUpdateCombat::UBTService_DBUpdateCombat()
{
	NodeName = TEXT("Update combat knowledge");
	bNotifyTick = true;
	bCallTickOnSearchStart = true;
	Interval = 0.15f;
	RandomDeviation = 0.02f;
}

void UBTService_DBUpdateCombat::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	if (auto* AI = Cast<ADBEnemyAIController>(OwnerComp.GetAIOwner()))
	{
		AI->RefreshCombatState();
	}
}
