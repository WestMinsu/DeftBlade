#include "Animation/DBAnimNotifyWeaponAttachment.h"

#include "Combat/DBCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

UDBAnimNotifyWeaponAttachment::UDBAnimNotifyWeaponAttachment()
{
	bIsNativeBranchingPoint = true;
}

void UDBAnimNotifyWeaponAttachment::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
	Super::BranchingPointNotify(Payload);
	AActor* Owner = Payload.SkelMeshComponent ? Payload.SkelMeshComponent->GetOwner() : nullptr;
	if (UDBCombatComponent* Combat = Owner ? Owner->FindComponentByClass<UDBCombatComponent>() : nullptr)
	{
		Combat->CommitWeaponAttachment(Payload.MontageInstanceID, bDrawn);
	}
}
