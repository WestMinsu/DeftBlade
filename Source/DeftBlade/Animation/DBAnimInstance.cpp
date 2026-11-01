#include "Animation/DBAnimInstance.h"

#include "Combat/DBTargetingComponent.h"
#include "Combat/DBCombatComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

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
		LocomotionIdle = Weapon->IdleAnimation;
		LocomotionBlendSpace = Weapon->LocomotionBlendSpace;
		WeaponCarryPose = Weapon->UpperBodyCarryPose ? Weapon->UpperBodyCarryPose : LocomotionIdle;
		const float TargetWeight = Weapon->UpperBodyCarryPose && !Combat->IsAttacking() ? 1.0f : 0.0f;
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
