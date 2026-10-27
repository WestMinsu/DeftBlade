#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DBAnimInstance.generated.h"

/** Limited lock-on glance; the AnimBP owns locomotion, slots and foot IK. */
UCLASS()
class DEFTBLADE_API UDBAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(BlueprintReadOnly, Category = "Targeting")
	FRotator LockNeckRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Targeting")
	FRotator LockHeadRotation = FRotator::ZeroRotator;
};
