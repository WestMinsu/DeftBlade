#pragma once

#include "CoreMinimal.h"
#include "Character/DBEnemyCharacter.h"
#include "DBBossCharacter.generated.h"

UCLASS()
class DEFTBLADE_API ADBBossCharacter : public ADBEnemyCharacter
{
	GENERATED_BODY()
public:
	ADBBossCharacter();
	void Activate(APawn* Player);
	virtual EDBAttackId SelectAttack(float Distance, bool bCombo) override;
	virtual float TakeDamage(
	    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	/** The hidden Manny mesh evaluates locomotion, slots and ragdoll once for all armour parts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TArray<TObjectPtr<USkeletalMeshComponent>> Armour;

private:
	void SharePose();
	bool bActive = false;
	EDBAttackId PreviousAttack = EDBAttackId::None;
};
