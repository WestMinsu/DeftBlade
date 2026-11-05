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
	inline const FName HasLastKnownLocation = TEXT("HasLastKnownLocation");
	inline const FName CanAct = TEXT("CanAct");
	inline const FName CanAttack = TEXT("CanAttack");
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
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float SearchDuration = 5;
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0.1"))
	float AttackCooldown = 0.85f;
	/** Game seconds after this enemy's last hit window before an ally may start its preparation. */
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float GroupAttackRecovery = 0.20f;

private:
	UFUNCTION()
	void PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
	void ForgetTarget();
	bool HasCommittedAttack() const;
	TWeakObjectPtr<AActor> TrackedTarget;
	bool bHasSight = false;
	double LastSeenAt = 0;
	double NextAttackAt = 0;
	FVector LastSeenLocation = FVector::ZeroVector;
};
