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
	Evade,
	LightAttack,
	HeavyAttack
};

/** One deliberate press. Id == 0 means the slot is empty. */
USTRUCT()
struct FDBBufferedAction
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	uint64 Id = 0;

	UPROPERTY(VisibleAnywhere)
	double ReceivedAt = 0.0;

	UPROPERTY(VisibleAnywhere)
	EDBActionState Action = EDBActionState::None;

	bool IsExpired(double Now, double Lifetime) const
	{
		return Id != 0 && Now >= ReceivedAt + Lifetime;
	}
};

/** Times are montage seconds, independent of playback rate. */
USTRUCT(BlueprintType)
struct FDBAttackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ActiveStart = 0.15f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ActiveEnd = 0.35f;

	bool IsValid() const;
	bool OverlapsActiveWindow(float Previous, float Current) const
	{
		return Current > Previous && Previous < ActiveEnd && Current > ActiveStart;
	}
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDBAttackWindowTraversed, float, float);

/** Owns action lifetime, one buffered input and finite ground/air evades. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDBCombatComponent();

	void RequestEvade();
	void RequestAttack(bool bHeavy);
	void SetEvadeHeld(bool bHeld, float SprintSpeed) { bEvadeHeld = bHeld; HeldGlideSpeed = SprintSpeed; }
	bool TryTransitionToSprint();
	void ResetAirEvade() { bAirEvadeUsed = false; }
	bool FinishAction(uint64 ExpectedActionId, bool bInterrupted = true);
	void ResetCombat();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInvulnerable() const;

	EDBActionState GetActionState() const { return ActionState; }
	uint64 GetActiveActionId() const { return ActiveActionId; }
	const FDBBufferedAction& GetBufferedAction() const { return BufferedAction; }
	bool IsAttacking() const { return ActionState == EDBActionState::LightAttack || ActionState == EDBActionState::HeavyAttack; }
	bool IsAttackWindowOpen() const;
	/** Swept montage-time interval; a long frame crossing the whole window still emits once. */
	FDBAttackWindowTraversed OnAttackWindowTraversed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Idle expiry; an active evade retains one follow-up until it can chain. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double InputBufferLifetime = 0.18;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
	FDBAttackDefinition LightAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
	FDBAttackDefinition HeavyAttack;

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
	FDBBufferedAction BufferedAction;

private:
	bool CanProcessInput() const;
	void TryStartEvade(double Now);
	void TryStartAttack();
	void SampleAttackWindow(float Position);
	void AttackBlendingOut(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId);
	void AttackEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId);
	void StopAttackMontage();
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
	UPROPERTY(Transient)
	FDBAttackDefinition ActiveAttack;
	int32 AttackMontageInstanceId = INDEX_NONE;
	float PreviousAttackPosition = 0.0f;
};
