#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BTTask_DBPatrol.generated.h"

class UCharacterMovementComponent;

UCLASS()
class DEFTBLADE_API UBTTask_DBPatrol : public UBTTask_MoveTo
{
	GENERATED_BODY()
public:
	UBTTask_DBPatrol();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(
	    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;

private:
	// The task is instanced per agent; interruption keeps its current destination.
	int32 PointIndex = 0;
	float PreviousSpeed = 0;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;
};
