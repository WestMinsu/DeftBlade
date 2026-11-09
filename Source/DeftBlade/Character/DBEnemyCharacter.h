#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/DBWeaponData.h"
#include "DBEnemyCharacter.generated.h"

class UBehaviorTree;
class UDBCombatComponent;
class UDBHealthComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimMontage;
class ATargetPoint;

UCLASS()
class DEFTBLADE_API ADBEnemyCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ADBEnemyCharacter();
	virtual float TakeDamage(
	    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
	bool CanAct() const;
	virtual EDBAttackId SelectAttack(float Distance, bool bCombo);
	UBehaviorTree* GetBehaviorTree() const
	{
		return BehaviorTree;
	}
	float GetAttackRange() const
	{
		return AttackRange;
	}
	const TArray<TObjectPtr<ATargetPoint>>& GetPatrolPoints() const
	{
		return PatrolPoints;
	}
	float GetPatrolSpeed() const
	{
		return PatrolSpeed;
	}

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
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
	/** Ordered level points. An empty route keeps this enemy stationary until alerted. */
	UPROPERTY(EditInstanceOnly, Category = "AI|Patrol")
	TArray<TObjectPtr<ATargetPoint>> PatrolPoints;
	UPROPERTY(EditAnywhere, Category = "AI|Patrol", meta = (ClampMin = "1"))
	float PatrolSpeed = 150.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> HitReactionMontage;
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> ParryReactionMontage;
	/** Visual flinch only; airborne movement and the landing recovery deadline remain authoritative. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> AirHitReactionMontage;
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> LandingRecoveryMontage;
	/** Minimum game seconds without AI actions after a grounded hit; each accepted hit refreshes it. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	float GroundHitStunDuration = 0.75f;
	/** Game seconds without AI actions after landing from a combat launch or an airborne hit. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	float LandingRecoveryDuration = 0.5f;

private:
	void UpdateHealthText(float Current, float Maximum);
	void HandleDeath();
	void ReactToParry(AActor* Defender);
	void PlayHitReaction(UAnimMontage* Montage);
	void StopReaction();
	void ReactionEnded(UAnimMontage* Montage, bool bInterrupted, int32 InstanceId);
	int32 ReactionInstanceId = INDEX_NONE;
	double HitStunEndsAt = 0.0;
	double LandingRecoveryEndsAt = 0.0;
	bool bRecoverAfterLanding = false;
};
