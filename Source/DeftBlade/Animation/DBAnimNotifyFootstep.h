#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "DBAnimNotifyFootstep.generated.h"

UCLASS()
class DEFTBLADE_API UDBAnimNotifyFootstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	    const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY(EditAnywhere, Category = "Footstep")
	FName FootBone = TEXT("foot_l");
};
