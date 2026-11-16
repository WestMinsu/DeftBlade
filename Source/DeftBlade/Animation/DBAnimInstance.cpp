#include "Animation/DBAnimInstance.h"

#include "Combat/DBTargetingComponent.h"
#include "Combat/DBCombatComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void UDBAnimInstance::PlayFootstep(FName FootBone)
{
	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	if (!Character || !GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->IsPaused() || FootstepSounds.IsEmpty()
	    || !Character->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}
	const UDBCombatComponent* Combat = Character->FindComponentByClass<UDBCombatComponent>();
	const float Speed = Character->GetVelocity().Size2D();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Speed < 20.0f || (Combat && Combat->GetActionState() != EDBActionState::None) || Now - LastFootstepAt < 0.10)
	{
		return;
	}
	// Blended locomotion clips can notify in the same frame. Keep the gate on this character's instance.
	const int32 Count = FootstepSounds.Num();
	int32 Index = FMath::RandHelper(Count);
	if (Count > 1 && FootstepSounds.IsValidIndex(LastFootstepSoundIndex))
	{
		Index = FMath::RandHelper(Count - 1);
		Index += Index >= LastFootstepSoundIndex ? 1 : 0;
	}
	USoundBase* Sound = FootstepSounds[Index];
	if (!Sound)
	{
		return;
	}
	const float Volume =
	    FMath::Clamp(0.35f + Speed / 900.0f, 0.45f, 1.0f) * (Character->IsPlayerControlled() ? 1.0f : 0.4f);
	UGameplayStatics::PlaySoundAtLocation(this, Sound, GetSkelMeshComponent()->GetSocketLocation(FootBone),
	    FRotator::ZeroRotator, Volume, FMath::FRandRange(0.96f, 1.04f), 0.0f, nullptr, nullptr, Character);
	LastFootstepAt = Now;
	LastFootstepSoundIndex = Index;
}

void UDBAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const APawn* Pawn = TryGetPawnOwner();
	const UDBTargetingComponent* Targeting = Pawn ? Pawn->FindComponentByClass<UDBTargetingComponent>() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	double DesiredLookYaw = 0;
	const double CurrentLookYaw = LockNeckRotation.Yaw + LockHeadRotation.Yaw;
	const UDBCombatComponent* Combat = Pawn ? Pawn->FindComponentByClass<UDBCombatComponent>() : nullptr;
	if (const UDBWeaponData* Weapon = Combat ? Combat->GetWeaponData() : nullptr)
	{
		const bool bDrawn = Combat->IsWeaponDrawn();
		LocomotionIdle =
		    !bDrawn && Weapon->HolsteredIdleAnimation ? Weapon->HolsteredIdleAnimation : Weapon->IdleAnimation;
		LocomotionBlendSpace = !bDrawn && Weapon->HolsteredLocomotionBlendSpace ? Weapon->HolsteredLocomotionBlendSpace
		                                                                        : Weapon->LocomotionBlendSpace;
		WeaponCarryPose = Weapon->UpperBodyCarryPose ? Weapon->UpperBodyCarryPose : LocomotionIdle;
		const float TargetWeight = bDrawn && Weapon->UpperBodyCarryPose && !Combat->IsAttacking() ? 1.0f : 0.0f;
		WeaponCarryWeight = FMath::FInterpConstantTo(
		    WeaponCarryWeight, TargetWeight, DeltaSeconds, TargetWeight > WeaponCarryWeight ? 8.0f : 20.0f);
	}
	if (Targeting && Targeting->IsLockedOn() && Character && Character->GetCharacterMovement()->IsMovingOnGround()
	    && (!Combat || Combat->GetActionState() == EDBActionState::None))
	{
		const double TargetYaw = (Targeting->GetTargetPoint() - Pawn->GetActorLocation()).Rotation().Yaw;
		const double RelativeYaw = FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw, TargetYaw);
		DesiredLookYaw = FMath::Clamp(RelativeYaw, -55.0, 55.0);
		// Keep the same shoulder while the target crosses directly behind the character.
		if (FMath::Abs(RelativeYaw) > 120.0 && FMath::Abs(CurrentLookYaw) > 1.0)
		{
			DesiredLookYaw = FMath::Sign(CurrentLookYaw) * 55.0;
		}
	}
	const double LookYaw = FMath::Lerp(CurrentLookYaw, DesiredLookYaw, 1.0 - FMath::Exp(-8.0 * DeltaSeconds));
	LockNeckRotation = FRotator(0, LookYaw * .35, 0);
	LockHeadRotation = FRotator(0, LookYaw * .65, 0);
}
