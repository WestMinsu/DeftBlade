#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DBCombatComponent.generated.h"

class UAnimMontage;
class UCurveFloat;

UENUM(BlueprintType)
enum class EDBActionState : uint8
{
	None,
	Evade
};

/** One deliberate press. Id == 0 means the slot is empty. */
USTRUCT()
struct FDBBufferedEvade
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	uint64 Id = 0;

	UPROPERTY(VisibleAnywhere)
	double ReceivedAt = 0.0;

	bool IsExpired(double Now, double Lifetime) const
	{
		return Id != 0 && Now >= ReceivedAt + Lifetime;
	}
};

/** Owns action lifetime, one buffered input and finite ground/air evades. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDBCombatComponent();

	void RequestEvade();
	void SetEvadeHeld(bool bHeld, float SprintSpeed) { bEvadeHeld = bHeld; HeldGlideSpeed = SprintSpeed; }
	bool TryTransitionToSprint();
	void ResetAirEvade() { bAirEvadeUsed = false; }
	bool FinishAction(uint64 ExpectedActionId, bool bInterrupted = true);
	void ResetCombat();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInvulnerable() const;

	EDBActionState GetActionState() const { return ActionState; }
	uint64 GetActiveActionId() const { return ActiveActionId; }
	const FDBBufferedEvade& GetBufferedEvade() const { return BufferedEvade; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Idle expiry; an active evade retains one follow-up until it can chain. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double InputBufferLifetime = 0.18;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double EvadeDuration = 0.80;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double BackwardEvadeDuration = 0.70;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "1"))
	float EvadeSpeed = 1250.0f;

	/** Allow the weight-transfer/recovery pose to play before the next push-off. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double EvadeChainStart = 0.68;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double BackwardEvadeChainStart = 0.60;

	/** Authored recovery time; held sliding delays this handoff. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double SprintTransitionStart = 0.55;

	/** Maximum extra movement/pose time from holding evade with a direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0", ClampMax = "0.5"))
	double MaxHeldSlideTime = 0.30;

	/** Normalized movement time -> speed scale, including push-off and recovery. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UCurveFloat> EvadeStrengthCurve;

	/** Optional backward timing; use the shared curve when unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UCurveFloat> BackwardEvadeStrengthCurve;

	/** Pose only; CMC root motion owns the evade's translation. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Animation")
	TObjectPtr<UAnimMontage> EvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Animation")
	TObjectPtr<UAnimMontage> BackwardEvadeMontage;

	/** Seconds from action start; intervals are [start, end). */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double InvulnerabilityStart = 0.05;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double InvulnerabilityEnd = 0.22;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Combat")
	EDBActionState ActionState = EDBActionState::None;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Combat")
	uint64 ActiveActionId = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Combat")
	FDBBufferedEvade BufferedEvade;

private:
	bool CanProcessInput() const;
	void TryStartEvade(double Now);
	void UpdateEvadeMovement(float DeltaTime);
	bool IsEvadeMontageActive() const;
	void StopEvadeMovement(bool bStopHorizontalVelocity = true);
	void StopEvadeMontage();

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Combat")
	double ActionStartedAt = 0.0;
	double MovementEndsAt = 0.0;
	double ChainStartsAt = 0.0;
	uint64 NextRequestId = 0;
	uint16 EvadeMotionId = 0;
	double HeldSlideTime = 0.0;
	FVector EvadeDirection = FVector::ForwardVector;
	float HeldGlideSpeed = 0.0f;
	bool bEvadeHeld = false;
	bool bBlendingToSprint = false;
	bool bBackwardEvade = false;
	bool bAirEvadeUsed = false;
};
