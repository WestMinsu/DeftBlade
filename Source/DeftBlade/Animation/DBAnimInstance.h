#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DBAnimInstance.generated.h"

/** Locomotion measurements only; the AnimBP composes poses, slots and foot IK. */
UCLASS()
class DEFTBLADE_API UDBAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bUseLockLocomotion = false;
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float LockForwardSpeed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float LockRightSpeed = 0;
};
