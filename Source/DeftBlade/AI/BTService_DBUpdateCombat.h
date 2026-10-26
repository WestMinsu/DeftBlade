#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_DBUpdateCombat.generated.h"

UCLASS()
class DEFTBLADE_API UBTService_DBUpdateCombat : public UBTService
{
	GENERATED_BODY()
public:
	UBTService_DBUpdateCombat();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
