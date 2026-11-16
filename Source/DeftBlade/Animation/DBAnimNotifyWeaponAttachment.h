#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "DBAnimNotifyWeaponAttachment.generated.h"

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
};
