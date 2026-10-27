#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DBTargetingComponent.generated.h"

/** Soft attack assistance and explicit hard lock-on share target eligibility. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBTargetingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDBTargetingComponent();
	AActor* SelectSoftTarget(const FVector& Direction);
	void AssistAttackFacing(const FVector& Direction);
	void ToggleLockOn();
	void ClearLockOn();
	void SwitchTarget(float Direction);
	void HandleStickSwitch(float Axis);
	bool IsLockedOn() const
	{
		return bLockOnActive && LockedTarget.IsValid();
	}
	AActor* GetLockedTarget() const
	{
		return LockedTarget.Get();
	}
	FVector GetTargetPoint() const;

protected:
	virtual void TickComponent(
	    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "1"))
	float SoftTargetRange = 350.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "0"))
	float MaxHeightDifference = 120.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "1", ClampMax = "89"))
	float SoftTargetHalfAngle = 50.0f;
	/** Maximum assistance relative to the player's intended direction, per attack. */
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxAttackYawCorrection = 35.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting|LockOn", meta = (ClampMin = "1"))
	float LockOnRange = 1000.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting|LockOn", meta = (ClampMin = "1"))
	float LockOnReleaseRange = 1300.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting|LockOn", meta = (ClampMin = "0"))
	float LockOnMaxHeight = 400.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting|LockOn", meta = (ClampMin = "0"))
	float OcclusionGraceTime = 0.35f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting|LockOn", meta = (ClampMin = "0", ClampMax = "45"))
	float LockCameraYawDeadZone = 25.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting|LockOn", meta = (ClampMin = "0"))
	float LockCameraTurnRate = 60.0f;

private:
	bool CanUseLockOn() const;
	bool IsEligible(AActor* Candidate, float Range, float Height) const;
	bool HasSight(AActor* Candidate, const FVector& Origin) const;
	void GatherCandidates(float Range, TArray<AActor*>& Candidates) const;
	bool ProjectCandidate(AActor* Candidate, FVector2D& Screen) const;
	void SetLockedTarget(AActor* Target);
	TWeakObjectPtr<AActor> SoftTarget;
	TWeakObjectPtr<AActor> LockedTarget;
	bool bLockOnActive = false;
	bool bStickSwitchArmed = false;
	float OccludedTime = 0.0f;
};
