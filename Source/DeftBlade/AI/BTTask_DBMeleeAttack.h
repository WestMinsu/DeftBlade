#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Combat/DBWeaponData.h"
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
	UPROPERTY(EditAnywhere, Category = "Attack", meta = (ClampMin = "0", ClampMax = "1"))
	float ComboChance = 0.5f;

	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(
	    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;

private:
	void CancelOwnedAttack();
	TWeakObjectPtr<UDBCombatComponent> Combat;
	uint64 RequestId = 0;
	double AimStartedAt = 0;
	EDBAttackId AttackId = EDBAttackId::None;
	bool bStarted = false;
};
