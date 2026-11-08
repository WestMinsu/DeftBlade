#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "DBEnemyAIController.generated.h"

class UAISenseConfig_Sight;

namespace DBEnemyBB
{
	inline const FName Target = TEXT("Target");
	inline const FName LastKnownLocation = TEXT("LastKnownLocation");
	inline const FName CombatLocation = TEXT("CombatLocation");
	inline const FName HasLastKnownLocation = TEXT("HasLastKnownLocation");
	inline const FName CanAct = TEXT("CanAct");
	inline const FName CanAttack = TEXT("CanAttack");
	inline const FName PatrolLocation = TEXT("PatrolLocation");
}

UCLASS()
class DEFTBLADE_API ADBEnemyAIController : public AAIController
{
	GENERATED_BODY()
public:
	ADBEnemyAIController();
	virtual void Tick(float DeltaSeconds) override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	void RefreshCombatState();
	bool CanStartAttack() const;
	void AttackFinished();
	void RememberAttacker(AActor* Attacker);
	void StopBehavior();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;
	/** Time spent searching while stopped; travel and hit reactions do not consume it. */
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float SearchDuration = 5;
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0.1"))
	float AttackCooldown = 0.85f;
	/** Game seconds after this enemy's last hit window before an ally may start its preparation. */
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float GroupAttackRecovery = 0.20f;
	UPROPERTY(EditDefaultsOnly, Category = "AI|Spacing", meta = (ClampMin = "200"))
	float OuterDistance = 420.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AI|Spacing", meta = (ClampMin = "84"))
	float EnemySeparation = 140.0f;

private:
	UFUNCTION()
	void PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
	void ForgetTarget();
	bool HasCommittedAttack() const;
	void UpdateClosePressure();
	FVector GetCombatLocation(const FVector& GroundTarget) const;
	UPROPERTY(VisibleInstanceOnly, Category = "AI|Spacing")
	bool bHasClosePressure = false;
	double ReconsiderPressureAt = 0.0;
	double PressureHeldUntil = 0.0;
	double HitPressureUntil = 0.0;
	TWeakObjectPtr<AActor> TrackedTarget;
	bool bHasSight = false;
	double SearchStartedAt = -1;
	double NextAttackAt = 0;
	FVector LastSeenLocation = FVector::ZeroVector;
};
