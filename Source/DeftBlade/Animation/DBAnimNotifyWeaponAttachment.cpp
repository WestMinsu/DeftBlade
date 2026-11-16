#include "Animation/DBAnimNotifyWeaponAttachment.h"

#include "Combat/DBCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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
		if (Combat->CommitWeaponAttachment(Payload.MontageInstanceID, bDrawn) && Sound)
		{
			UGameplayStatics::PlaySoundAtLocation(Payload.SkelMeshComponent, Sound,
			    Payload.SkelMeshComponent->GetSocketLocation(TEXT("hand_r")), VolumeMultiplier);
		}
	}
}
