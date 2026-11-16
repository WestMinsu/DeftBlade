#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DBAnimInstance.generated.h"

class UAnimSequence;
class UBlendSpace;
class USoundBase;

/** Weapon locomotion inputs and limited lock-on glance; slots and foot IK stay in the AnimBP. */
UCLASS()
class DEFTBLADE_API UDBAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void PlayFootstep(FName FootBone);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	/** Class defaults are preview fallbacks when the AnimBP editor has no character owner. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> LocomotionIdle;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> WeaponCarryPose;
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float WeaponCarryWeight = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Targeting")
	FRotator LockNeckRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Targeting")
	FRotator LockHeadRotation = FRotator::ZeroRotator;
	UPROPERTY(EditDefaultsOnly, Category = "Audio")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

private:
	double LastFootstepAt = -1.0;
	int32 LastFootstepSoundIndex = INDEX_NONE;
};
