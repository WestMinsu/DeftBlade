#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DBWeaponData.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class UBlendSpace;
class UParticleSystem;
class USoundBase;
class UStaticMesh;

UENUM(BlueprintType)
enum class EDBHitTraceSource : uint8
{
	Weapon,
	LeftHand,
	RightHand
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

/** Times are montage seconds, independent of playback rate. */
USTRUCT(BlueprintType)
struct FDBAttackWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float Start = 0.15f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float End = 0.35f;

	/** Each swing selects its own geometry; separate fists share the same damage/actor filter. */
	UPROPERTY(EditAnywhere)
	EDBHitTraceSource TraceSource = EDBHitTraceSource::Weapon;

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

	/** Attack on the requested weapon after this move's ComboStart. None disables switching here. */
	UPROPERTY(EditAnywhere)
	EDBAttackId SwitchAttack = EDBAttackId::None;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float PlayRate = 1.0f;

	/** Separate swings in one motion may each damage the same target once. */
	UPROPERTY(EditAnywhere)
	TArray<FDBAttackWindow> HitWindows = {FDBAttackWindow()};

	/** Light input can arm a parry during this move; Light1 also enables idle parries. */
	UPROPERTY(EditAnywhere)
	bool bCanParry = false;

	/** Damage still applies; this interval resists flinch and forced launch. Empty disables it. */
	UPROPERTY(EditAnywhere)
	FDBAttackWindow StaggerResistanceWindow = {0.0f, 0.0f};

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

	/** Optional feedback at an accepted damage contact, including a killing blow. */
	UPROPERTY(EditAnywhere, Category = "Hit Feedback")
	TObjectPtr<UParticleSystem> HitEffect;

	UPROPERTY(EditAnywhere, Category = "Hit Feedback")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, Category = "Hit Feedback", meta = (ClampMin = "0.01"))
	float HitEffectScale = 1.0f;

	bool IsValid() const;
};

/** Shared authoring data for one weapon moveset. Action state stays on the Combat component. */
UCLASS(BlueprintType)
class DEFTBLADE_API UDBWeaponData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Optional for movesets whose hit windows use only hands. */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	FName AttachSocket = TEXT("WeaponSocket");

	UPROPERTY(EditAnywhere, Category = "Weapon")
	FTransform RelativeTransform = FTransform::Identity;

	/** Existing locomotion/slot AnimBP preset; not changed during an action. */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSubclassOf<UAnimInstance> AnimationClass;

	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UAnimSequence> IdleAnimation;

	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	/** Optional arm pose for carrying a long weapon outside attacks. */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UAnimSequence> UpperBodyCarryPose;

	/** Radius of weapon-segment or hand sphere sweeps, in cm. */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.5"))
	float BladeTraceRadius = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	TArray<FDBAttackDefinition> Attacks;
};
