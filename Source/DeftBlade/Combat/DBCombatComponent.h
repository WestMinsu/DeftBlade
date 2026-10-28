#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DBCombatComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UCurveFloat;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EDBActionState : uint8
{
	None,
	Evade,
	LightAttack,
	HeavyAttack,
	HitReaction
};

/** A jump is a request; CMC owns its movement, so it is not a combat state. */
UENUM()
enum class EDBActionInput : uint8
{
	None,
	Evade,
	LightAttack,
	HeavyAttack,
	Jump,
	Launcher
};

UENUM(BlueprintType)
enum class EDBAttackId : uint8
{
	None,
	Light1,
	Light2,
	Light3,
	Light4,
	Heavy1,
	Heavy2,
	Heavy3,
	Heavy4,
	Light1Heavy,
	Light2Heavy,
	AirLight1,
	AirLight2,
	AirLight3,
	Launcher,
	AirHeavy
};

/** One pending action request. Id == 0 means the slot is empty. */
USTRUCT()
struct FDBBufferedAction
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	uint64 Id = 0;

	UPROPERTY(VisibleAnywhere)
	double ReceivedAt = 0.0;

	UPROPERTY(VisibleAnywhere)
	EDBActionInput Input = EDBActionInput::None;

	bool IsExpired(double Now, double Lifetime) const
	{
		return Id != 0 && Now >= ReceivedAt + Lifetime;
	}
};

/** Times are montage seconds, independent of playback rate. */
USTRUCT(BlueprintType)
struct FDBAttackWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float Start = 0.15f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float End = 0.35f;

	bool Overlaps(float Previous, float Current) const
	{
		return Current > Previous && Previous < End && Current > Start;
	}
};

USTRUCT(BlueprintType)
struct FDBAttackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	EDBAttackId Id = EDBAttackId::None;

	UPROPERTY(EditAnywhere)
	EDBAttackId NextLight = EDBAttackId::None;

	UPROPERTY(EditAnywhere)
	EDBAttackId NextHeavy = EDBAttackId::None;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float PlayRate = 1.0f;

	/** Separate swings in one motion may each damage the same target once. */
	UPROPERTY(EditAnywhere)
	TArray<FDBAttackWindow> HitWindows = {FDBAttackWindow()};

	/** Earliest next attack; always after this motion's last hit window. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ComboStart = 0.65f;

	/** Dodge may interrupt startup; jump/movement wait until the final hit ends. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float EvadeCancelStart = 0.10f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float JumpCancelStart = 0.55f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float MoveCancelStart = 0.60f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float Damage = 25.0f;

	bool IsValid() const;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDBAttackWindowTraversed, float, float);
DECLARE_MULTICAST_DELEGATE(FDBAttackStarted);

/** Owns action lifetime, one buffered input and finite ground/air evades. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDBCombatComponent();

	void RequestEvade();
	void RequestAttack(bool bHeavy);
	void RequestJump();
	void ReactToHit(UAnimMontage* Montage);
	bool CanJumpFromAction() const;
	void SetWeaponMesh(UStaticMeshComponent* InWeapon)
	{
		WeaponMesh = InWeapon;
	}
	void SetEvadeHeld(bool bHeld, float SprintSpeed)
	{
		bEvadeHeld = bHeld;
		HeldGlideSpeed = SprintSpeed;
	}
	bool TryTransitionToSprint();
	void HandleLanding();
	void ResetAirEvade()
	{
		bAirEvadeUsed = false;
	}
	bool FinishAction(uint64 ExpectedActionId, bool bInterrupted = true);
	void ResetCombat();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInvulnerable() const;

	EDBActionState GetActionState() const
	{
		return ActionState;
	}
	uint64 GetActiveActionId() const
	{
		return ActiveActionId;
	}
	EDBAttackId GetAttackId() const
	{
		return IsAttacking() ? ActiveAttack.Id : EDBAttackId::None;
	}
	const FDBBufferedAction& GetBufferedAction() const
	{
		return BufferedAction;
	}
	bool IsAttacking() const
	{
		return ActionState == EDBActionState::LightAttack || ActionState == EDBActionState::HeavyAttack;
	}
	bool IsAttackWindowOpen() const;
	const FDBAttackDefinition* FindAttack(EDBAttackId Id) const;
	/** Swept montage-time interval; a long frame crossing the whole window still emits once. */
	FDBAttackWindowTraversed OnAttackWindowTraversed;
	/** Accepted montage start, including buffered combo links; before the initial blade snapshot. */
	FDBAttackStarted OnAttackStarted;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
	    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Idle expiry; active attacks/evades retain one compatible follow-up. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double InputBufferLifetime = 0.18;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
	TArray<FDBAttackDefinition> Attacks;

	/** Attack exits the evade after its fixed i-frame interval. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double EvadeAttackCancelStart = 0.26;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	double EvadeJumpCancelStart = 0.10;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace", meta = (ClampMin = "0.5"))
	float BladeTraceRadius = 3.0f;

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
	void QueueInput(EDBActionInput Input);
	EDBAttackId NextAttack(EDBActionInput Input) const;
	bool IsAttackMovementValid(EDBAttackId Id) const;
	void TryStartJump();
	void TryStartEvade(double Now);
	void TryStartAttack();
	void UpdateAttackMovement(float DeltaTime);
	void StartAirMovement(float Duration);
	void HoldAirborne(ACharacter* Attacker);
	void StopAirMovement();
	void SampleAttackWindow(float Position);
	void AttackBlendingOut(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId);
	void AttackEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId);
	void StopAttackMontage();
	void HitReactionBlendingOut(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId);
	void HitReactionEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId);
	void StopHitReaction();
	void BonesFinalized();
	bool ConfigureBlade();
	FTransform GetWeaponTransform() const;
	FTransform SampleWeaponTransform(
	    float Position, float CurrentPosition, const FTransform& CurrentMesh, const FTransform& CurrentWeapon) const;
	void TraceAttack(float Previous, float Current, const FTransform& CurrentMesh, const FTransform& CurrentWeapon);
	void SweepBlade(const FTransform& From, const FTransform& To, uint64 ActionId);
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
	uint16 AirMotionId = 0;
	TWeakObjectPtr<ACharacter> JuggleAttacker;
	double JuggleHeight = 0.0;
	double HeldSlideTime = 0.0;
	FVector EvadeDirection = FVector::ForwardVector;
	float HeldGlideSpeed = 0.0f;
	bool bEvadeHeld = false;
	bool bBlendingToSprint = false;
	bool bBackwardEvade = false;
	bool bAirEvadeUsed = false;
	bool bAirLightComboUsed = false;
	UPROPERTY(Transient)
	FDBAttackDefinition ActiveAttack;
	int32 ActiveHitWindow = INDEX_NONE;
	int32 AttackMontageInstanceId = INDEX_NONE;
	int32 ReactionMontageInstanceId = INDEX_NONE;
	float PreviousAttackPosition = 0.0f;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WeaponMesh;
	FDelegateHandle PoseFinalizedHandle;
	FTransform PreviousMeshTransform;
	FTransform PreviousWeaponTransform;
	FVector BladeBaseLocal = FVector::ZeroVector;
	FVector BladeTipLocal = FVector::ZeroVector;
	TSet<TWeakObjectPtr<AActor>> HitActors;
	bool bAttackEndedPending = false;
	double GroundJumpPressedAt = -1.0;
	bool bAttackMovementStarted = false;
	bool bAttackLanded = false;
};
