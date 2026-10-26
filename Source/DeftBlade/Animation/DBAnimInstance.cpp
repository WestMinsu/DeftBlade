#include "Animation/DBAnimInstance.h"

#include "Combat/DBTargetingComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UDBAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const APawn* Pawn = TryGetPawnOwner();
	const UDBTargetingComponent* Targeting = Pawn ? Pawn->FindComponentByClass<UDBTargetingComponent>() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	bUseLockLocomotion =
	    Targeting && Targeting->IsLockedOn() && Character && Character->GetCharacterMovement()->IsMovingOnGround();
	const FVector LocalVelocity =
	    Pawn ? Pawn->GetActorRotation().UnrotateVector(Pawn->GetVelocity()) : FVector::ZeroVector;
	LockForwardSpeed = LocalVelocity.X;
	LockRightSpeed = LocalVelocity.Y;
}
