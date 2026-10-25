#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_DBMeleeAttack.generated.h"

class UDBCombatComponent;

UCLASS()
class DEFTBLADE_API UBTTask_DBMeleeAttack : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_DBMeleeAttack();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
private:
	void CancelOwnedAttack();
	TWeakObjectPtr<UDBCombatComponent> Combat;
	uint64 RequestId = 0;
	bool bStarted = false;
};
