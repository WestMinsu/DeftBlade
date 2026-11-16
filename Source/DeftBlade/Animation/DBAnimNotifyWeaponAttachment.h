#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "DBAnimNotifyWeaponAttachment.generated.h"

class USoundBase;

/** Commits the hand/back contact of the current weapon-presentation montage. */
UCLASS()
class DEFTBLADE_API UDBAnimNotifyWeaponAttachment : public UAnimNotify
{
	GENERATED_BODY()

public:
	UDBAnimNotifyWeaponAttachment();
	virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	bool bDrawn = true;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = "0"))
	float VolumeMultiplier = 0.5f;
};
