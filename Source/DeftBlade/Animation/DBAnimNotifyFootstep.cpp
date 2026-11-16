#include "Animation/DBAnimNotifyFootstep.h"

#include "Animation/DBAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UDBAnimNotifyFootstep::Notify(
    USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (UDBAnimInstance* Anim = MeshComp ? Cast<UDBAnimInstance>(MeshComp->GetAnimInstance()) : nullptr)
	{
		Anim->PlayFootstep(FootBone);
	}
}
