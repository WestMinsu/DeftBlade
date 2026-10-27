#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DBEnemyCharacter.generated.h"

class UBehaviorTree;
class UDBCombatComponent;
class UDBHealthComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimMontage;

UCLASS()
class DEFTBLADE_API ADBEnemyCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ADBEnemyCharacter();
	virtual float TakeDamage(
	    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
	bool CanAct() const;
	UBehaviorTree* GetBehaviorTree() const
	{
		return BehaviorTree;
	}
	float GetAttackRange() const
	{
		return AttackRange;
	}

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDBCombatComponent> CombatComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDBHealthComponent> HealthComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UTextRenderComponent> HealthText;
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TObjectPtr<UBehaviorTree> BehaviorTree;
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "50"))
	float AttackRange = 170.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> HitReactionMontage;

private:
	void UpdateHealthText(float Current, float Maximum);
	void HandleDeath();
	void StopReaction();
	void ReactionEnded(UAnimMontage* Montage, bool bInterrupted, int32 InstanceId);
	int32 ReactionInstanceId = INDEX_NONE;
};
