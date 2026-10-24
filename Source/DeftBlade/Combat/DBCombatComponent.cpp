#include "Combat/DBCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBCombat, Log, All);

UDBCombatComponent::UDBCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDBCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &ThisClass::ResetCombat);
}

bool UDBCombatComponent::CanProcessInput() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	AController* Controller = Character ? Character->GetController() : nullptr;
	if (!Controller || GetWorld()->IsPaused() || Controller->IsMoveInputIgnored())
	{
		return false;
	}
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
		UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
		if (ViewportClient && (ViewportClient->IgnoreInput()
			|| (ViewportClient->Viewport && !ViewportClient->Viewport->HasFocus())))
		{
			return false;
		}
	}
	return true;
}

void UDBCombatComponent::RequestEvade()
{
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}
	if (CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->IsFalling() && (bAirEvadeUsed || EvadeMotionId != 0))
	{
		// A rejected air press must not replay automatically on landing.
		bAirEvadeUsed = true;
		BufferedEvade = {};
		return;
	}
	BufferedEvade = {++NextRequestId, GetWorld()->GetTimeSeconds()};
	UE_LOG(LogDBCombat, Verbose, TEXT("%s queued #%llu at %.3f"), *GetOwner()->GetName(),
		BufferedEvade.Id, BufferedEvade.ReceivedAt);
}

bool UDBCombatComponent::TryTransitionToSprint()
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bLeavingBackward = bBackwardEvade && FVector::DotProduct(
		Character->GetPendingMovementInputVector().GetSafeNormal2D(), Character->GetActorForwardVector()) > 0.0;
	const double HandoffTime = bLeavingBackward ? InvulnerabilityEnd : FMath::Max(SprintTransitionStart + HeldSlideTime, InvulnerabilityEnd);
	if (!CanProcessInput() || !Movement->IsMovingOnGround() || ActionState != EDBActionState::Evade
		|| EvadeMotionId == 0 || BufferedEvade.Id != 0 || GetWorld()->GetTimeSeconds() >= MovementEndsAt
		|| GetWorld()->GetTimeSeconds() < ActionStartedAt + HandoffTime)
	{
		return false;
	}
	// CMC accelerates the retained recovery velocity into the sprint in this same frame.
	StopEvadeMovement(false);
	if (bLeavingBackward)
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance(); AnimInstance && BackwardEvadeMontage)
		{
			// Fade the backward pose before retained backward momentum reverses under CMC acceleration.
			AnimInstance->Montage_Stop(0.06f, BackwardEvadeMontage);
		}
	}
	else if (!bBlendingToSprint)
	{
		StopEvadeMontage();
	}
	FinishAction(ActiveActionId, false);
	return true;
}

void UDBCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (EvadeMotionId != 0 && CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->IsFalling())
	{
		// A ground evade carried off a ledge already occupies this airborne evade.
		bAirEvadeUsed = true;
	}
	// Reserve one follow-up throughout this evade, including its animation recovery.
	if (ActionState != EDBActionState::Evade && BufferedEvade.IsExpired(Now, InputBufferLifetime))
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s expired #%llu at %.3f"), *GetOwner()->GetName(), BufferedEvade.Id, Now);
		BufferedEvade = {};
	}
	const bool bCanChain = ActionState == EDBActionState::Evade && Now >= ChainStartsAt;
	if (BufferedEvade.Id != 0 && (bCanChain || (ActionState == EDBActionState::None && !IsEvadeMontageActive())))
	{
		// Start at the authored recovery gate, keeping its remaining momentum.
		TryStartEvade(Now);
	}
	if (ActionState == EDBActionState::Evade)
	{
		UpdateEvadeMovement(DeltaTime);
	}
	if (ActionState == EDBActionState::Evade && bBlendingToSprint && !bEvadeHeld && BufferedEvade.Id == 0)
	{
		// A late release finishes the locomotion blend without zeroing its momentum.
		StopEvadeMovement(false);
		FinishAction(ActiveActionId, false);
	}
	if (ActionState == EDBActionState::Evade && Now >= MovementEndsAt)
	{
		const bool bMovementJustEnded = EvadeMotionId != 0;
		StopEvadeMovement();
		if (bMovementJustEnded)
		{
			// Finish the authored motion, then blend to locomotion.
			StopEvadeMontage();
		}
		if (!IsEvadeMontageActive() || !CastChecked<ACharacter>(GetOwner())->GetPendingMovementInputVector().IsNearlyZero())
		{
			FinishAction(ActiveActionId, false);
		}
	}
	if (ActionState == EDBActionState::Evade && bBackwardEvade)
	{
		// Keep the backward facing until its pose exits. Forward input is handled by the handoff above.
		CastChecked<ACharacter>(GetOwner())->ConsumeMovementInputVector();
	}
}

void UDBCombatComponent::UpdateEvadeMovement(float DeltaTime)
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const TSharedPtr<FRootMotionSource_ConstantForce> Motion = StaticCastSharedPtr<FRootMotionSource_ConstantForce>(
		Movement->GetRootMotionSourceByID(EvadeMotionId));
	if (!Motion)
	{
		return;
	}

	const FVector Direction = Character->GetPendingMovementInputVector().GetSafeNormal2D();
	if (!Direction.IsNearlyZero() && (!bBackwardEvade || FVector::DotProduct(Direction, Character->GetActorForwardVector()) <= 0.0))
	{
		EvadeDirection = Direction;
	}

	// Stretch only the glide, preserving push-off, recovery and the wall-clock i-frame interval.
	const double PoseTime = GetWorld()->GetTimeSeconds() - ActionStartedAt - HeldSlideTime;
	const double ExtraTime = bEvadeHeld && !Direction.IsNearlyZero() && BufferedEvade.Id == 0
		&& PoseTime >= 0.18 && PoseTime < SprintTransitionStart
		? FMath::Min(DeltaTime * 0.5, FMath::Max(0.0, MaxHeldSlideTime - HeldSlideTime)) : 0.0;
	if (ExtraTime > 0.0)
	{
		// CMC still performs collision/gravity; slowing this finite source also slows its speed curve.
		Motion->SetTime(FMath::Max(0.0f, Motion->GetTime() - static_cast<float>(ExtraTime)));
		HeldSlideTime += ExtraTime;
		MovementEndsAt += ExtraTime;
		ChainStartsAt += ExtraTime;
	}
	UCurveFloat* Curve = bBackwardEvade && BackwardEvadeStrengthCurve ? BackwardEvadeStrengthCurve : EvadeStrengthCurve;
	float Speed = EvadeSpeed * (Curve ? Curve->GetFloatValue(FMath::Clamp(Motion->GetTime() / Motion->Duration, 0.0f, 1.0f)) : 1.0f);
	const bool bHeldForward = bEvadeHeld && !bBackwardEvade && !Direction.IsNearlyZero() && BufferedEvade.Id == 0;
	if (bHeldForward && PoseTime >= 0.18)
	{
		// Coast into locomotion speed instead of braking to a near stop and accelerating again.
		Speed = FMath::Max(Speed, HeldGlideSpeed);
	}
	if (!bBackwardEvade && Motion->GetTime() >= 0.18f)
	{
		Speed = FMath::FInterpConstantTo(Motion->Force.Size2D(), Speed, DeltaTime, EvadeSpeed * 5.0f);
	}
	Motion->Force = EvadeDirection * Speed;
	if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance())
	{
		if (UAnimMontage* Montage = bBackwardEvade ? BackwardEvadeMontage.Get() : EvadeMontage.Get())
		{
			if (!bBlendingToSprint && bHeldForward && Movement->IsMovingOnGround() && PoseTime >= 0.40)
			{
				// Locomotion already has current acceleration and velocity underneath the slot.
				bBlendingToSprint = true;
				AnimInstance->Montage_SetPlayRate(Montage, 1.0f);
				AnimInstance->Montage_Stop(0.22f, Montage);
			}
			else if (!bBlendingToSprint)
			{
				AnimInstance->Montage_SetPlayRate(Montage, DeltaTime > 0.0f ? 1.0f - ExtraTime / DeltaTime : 1.0f);
			}
		}
	}
}

void UDBCombatComponent::TryStartEvade(double Now)
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement->IsActive() || Movement->MovementMode == MOVE_None)
	{
		return;
	}
	if (Movement->IsFalling() && bAirEvadeUsed)
	{
		BufferedEvade = {};
		return;
	}
	FVector Direction = Character->GetPendingMovementInputVector().GetSafeNormal2D();
	const bool bStartBackward = Direction.IsNearlyZero();
	if (bStartBackward)
	{
		Direction = -Character->GetActorForwardVector().GetSafeNormal2D();
	}
	const double Duration = bStartBackward ? BackwardEvadeDuration : EvadeDuration;
	const double ChainStart = bStartBackward ? BackwardEvadeChainStart : EvadeChainStart;
	if (!FMath::IsFinite(Duration) || Duration <= 0.0 || Duration > MAX_flt
		|| !FMath::IsFinite(EvadeSpeed) || EvadeSpeed <= 0.0f
		|| !FMath::IsFinite(InvulnerabilityStart) || !FMath::IsFinite(InvulnerabilityEnd)
		|| InvulnerabilityStart < 0.0 || InvulnerabilityEnd <= InvulnerabilityStart
		|| !FMath::IsFinite(ChainStart) || InvulnerabilityEnd >= ChainStart || ChainStart >= Duration)
	{
		return;
	}

	TSharedPtr<FRootMotionSource_ConstantForce> Motion = MakeShared<FRootMotionSource_ConstantForce>();
	Motion->InstanceName = TEXT("DBEvade");
	Motion->Priority = 100;
	Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
	Motion->Duration = static_cast<float>(Duration);
	Motion->Force = Direction * EvadeSpeed; // ConstantForce supplies velocity in cm/s.
	Motion->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Motion->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
	const uint16 NewMotionId = Movement->ApplyRootMotionSource(Motion);
	if (NewMotionId == 0)
	{
		return;
	}
	if (EvadeMotionId != 0)
	{
		// The previous action's motion is ours; preserve momentum while the new source takes over.
		Movement->RemoveRootMotionSourceByID(EvadeMotionId);
	}
	EvadeMotionId = NewMotionId;
	bBackwardEvade = bStartBackward;
	EvadeDirection = Direction;
	bBlendingToSprint = false;
	if (Movement->IsFalling())
	{
		bAirEvadeUsed = true;
	}
	ActiveActionId = BufferedEvade.Id;
	ActionStartedAt = Now;
	HeldSlideTime = 0.0;
	MovementEndsAt = Now + Duration;
	ChainStartsAt = Now + ChainStart;
	ActionState = EDBActionState::Evade;
	BufferedEvade = {};
	if (!bBackwardEvade)
	{
		Character->SetActorRotation(Direction.Rotation());
	}
	UAnimMontage* Montage = bBackwardEvade ? BackwardEvadeMontage : EvadeMontage;
	if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance(); AnimInstance && Montage)
	{
		// The default stops this montage group with BlendIn, so old cycles fade out instead of accumulating.
		AnimInstance->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength);
	}
	UE_LOG(LogDBCombat, Log, TEXT("%s started Evade #%llu at %.3f, movement ends %.3f"),
		*GetOwner()->GetName(), ActiveActionId, Now, MovementEndsAt);
}

bool UDBCombatComponent::IsInvulnerable() const
{
	if (ActionState != EDBActionState::Evade)
	{
		return false;
	}
	const double Elapsed = GetWorld()->GetTimeSeconds() - ActionStartedAt;
	return Elapsed >= InvulnerabilityStart && Elapsed < InvulnerabilityEnd;
}

bool UDBCombatComponent::FinishAction(uint64 ExpectedActionId, bool bInterrupted)
{
	if (ActionState == EDBActionState::None || ExpectedActionId != ActiveActionId)
	{
		return false;
	}
	UE_LOG(LogDBCombat, Log, TEXT("%s finished Evade #%llu at %.3f"),
		*GetOwner()->GetName(), ActiveActionId, GetWorld()->GetTimeSeconds());
	StopEvadeMovement();
	if (bInterrupted)
	{
		StopEvadeMontage();
		BufferedEvade = {};
	}
	ActionState = EDBActionState::None;
	ActiveActionId = 0;
	ActionStartedAt = 0.0;
	MovementEndsAt = 0.0;
	ChainStartsAt = 0.0;
	HeldSlideTime = 0.0;
	bBlendingToSprint = false;
	return true;
}

bool UDBCombatComponent::IsEvadeMontageActive() const
{
	if (const UAnimInstance* AnimInstance = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance())
	{
		// Active-map queries become false as blend-out begins; the pose still contributes then.
		return (EvadeMontage && AnimInstance->GetInstanceForMontage(EvadeMontage))
			|| (BackwardEvadeMontage && AnimInstance->GetInstanceForMontage(BackwardEvadeMontage));
	}
	return false;
}

void UDBCombatComponent::StopEvadeMovement(bool bStopHorizontalVelocity)
{
	if (EvadeMotionId != 0)
	{
		UCharacterMovementComponent* Movement = CastChecked<ACharacter>(GetOwner())->GetCharacterMovement();
		bAirEvadeUsed |= Movement->IsFalling();
		Movement->RemoveRootMotionSourceByID(EvadeMotionId);
		// Retain jump/fall velocity and the current movement mode during pose recovery.
		if (bStopHorizontalVelocity)
		{
			Movement->Velocity.X = 0.0;
			Movement->Velocity.Y = 0.0;
		}
		EvadeMotionId = 0;
	}
}

void UDBCombatComponent::StopEvadeMontage()
{
	if (UAnimInstance* AnimInstance = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance())
	{
		for (UAnimMontage* Montage : {EvadeMontage.Get(), BackwardEvadeMontage.Get()})
		{
			if (Montage)
			{
				AnimInstance->Montage_Stop(Montage->BlendOut.GetBlendTime(), Montage);
			}
		}
	}
}

void UDBCombatComponent::ResetCombat()
{
	if (ActiveActionId != 0 || BufferedEvade.Id != 0)
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s reset active #%llu / pending #%llu"),
			*GetOwner()->GetName(), ActiveActionId, BufferedEvade.Id);
	}
	FinishAction(ActiveActionId, false);
	// Reset also stops any residual blend-out after an interruption.
	StopEvadeMontage();
	BufferedEvade = {};
	bEvadeHeld = false;
}

void UDBCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	ResetCombat();
	Super::EndPlay(EndPlayReason);
}
