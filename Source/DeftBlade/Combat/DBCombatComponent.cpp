#include "Combat/DBCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBTargetingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "GenericTeamAgentInterface.h"
#include "Misc/CoreDelegates.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBCombat, Log, All);

namespace
{
	bool IsAttackInput(EDBActionInput Input)
	{
		return Input == EDBActionInput::LightAttack || Input == EDBActionInput::HeavyAttack
		       || Input == EDBActionInput::Launcher;
	}
	bool IsAirLight(EDBAttackId Id)
	{
		return Id == EDBAttackId::AirLight1 || Id == EDBAttackId::AirLight2 || Id == EDBAttackId::AirLight3;
	}
	int32 InputPriority(EDBActionInput Input)
	{
		return Input == EDBActionInput::Evade                                       ? 3
		       : Input == EDBActionInput::Jump || Input == EDBActionInput::Launcher ? 2
		                                                                            : 1;
	}
}

UDBCombatComponent::UDBCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDBCombatComponent::InitializeWeapon(UStaticMeshComponent* InWeapon)
{
	WeaponMesh = InWeapon;
	if (!WeaponMesh)
	{
		return;
	}
	WeaponMesh->SetStaticMesh(WeaponData ? WeaponData->Mesh : nullptr);
	if (!WeaponData)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	WeaponMesh->AttachToComponent(
	    Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponData->AttachSocket);
	WeaponMesh->SetRelativeTransform(WeaponData->RelativeTransform);
	if (WeaponData->AnimationClass && Mesh->GetAnimClass() != WeaponData->AnimationClass)
	{
		Mesh->SetAnimInstanceClass(WeaponData->AnimationClass);
	}
}

void UDBCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &ThisClass::ResetCombat);
	USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	PoseFinalizedHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
	    FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ThisClass::BonesFinalized));
}

bool UDBCombatComponent::CanProcessInput() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UDBHealthComponent* Health = GetOwner()->FindComponentByClass<UDBHealthComponent>();
	if (Health && Health->IsDead())
	{
		return false;
	}
	AController* Controller = Character ? Character->GetController() : nullptr;
	if (!Controller || GetWorld()->IsPaused() || Controller->IsMoveInputIgnored())
	{
		return false;
	}
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
		UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
		if (ViewportClient
		    && (ViewportClient->IgnoreInput() || (ViewportClient->Viewport && !ViewportClient->Viewport->HasFocus())))
		{
			return false;
		}
	}
	return true;
}

void UDBCombatComponent::RequestEvade()
{
	QueueInput(EDBActionInput::Evade);
}

void UDBCombatComponent::RequestAttack(bool bHeavy)
{
	QueueInput(bHeavy ? EDBActionInput::HeavyAttack : EDBActionInput::LightAttack);
}

void UDBCombatComponent::RequestJump()
{
	QueueInput(EDBActionInput::Jump);
}

void UDBCombatComponent::QueueInput(EDBActionInput Input)
{
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}
	if (ActionState == EDBActionState::HitReaction)
	{
		return;
	}
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bGrounded = Character->GetCharacterMovement()->IsMovingOnGround();
	if (Input == EDBActionInput::Jump && bGrounded)
	{
		GroundJumpPressedAt = Now;
		const bool bQueuedAttack = BufferedAction.Id != 0 && BufferedAction.Input == EDBActionInput::HeavyAttack
		                           && Now - BufferedAction.ReceivedAt <= 0.10;
		const bool bFreshGroundAttack = ActionState == EDBActionState::HeavyAttack
		                                && ActiveAttack.Id != EDBAttackId::Launcher
		                                && ActiveAttack.Id != EDBAttackId::AirHeavy && Now - ActionStartedAt <= 0.10;
		if (bQueuedAttack || bFreshGroundAttack)
		{
			Input = EDBActionInput::Launcher;
		}
	}
	else if (Input == EDBActionInput::HeavyAttack && GroundJumpPressedAt >= 0.0 && Now - GroundJumpPressedAt <= 0.10
	         && ActiveAttack.Id != EDBAttackId::Launcher && (bGrounded || Character->JumpCurrentCount == 1))
	{
		Input = EDBActionInput::Launcher;
	}
	if (IsAttackInput(Input) && NextAttack(Input) == EDBAttackId::None)
	{
		return;
	}
	if (Input == EDBActionInput::Jump
	    && (Character->JumpCurrentCount >= Character->JumpMaxCount
	        || !Character->GetCharacterMovement()->CanAttemptJump()))
	{
		return;
	}
	if (Input == EDBActionInput::Evade && Character->GetCharacterMovement()->IsFalling()
	    && (bAirEvadeUsed || EvadeMotionId != 0))
	{
		// A rejected air press must not replay automatically on landing.
		bAirEvadeUsed = true;
		return;
	}
	if (BufferedAction.Id != 0
	    && (ActionState != EDBActionState::None || !BufferedAction.IsExpired(Now, InputBufferLifetime))
	    && InputPriority(BufferedAction.Input) > InputPriority(Input))
	{
		return;
	}
	BufferedAction = {++NextRequestId, Now, Input};
}

EDBAttackId UDBCombatComponent::NextAttack(EDBActionInput Input) const
{
	if (!IsAttackInput(Input))
	{
		return EDBAttackId::None;
	}
	if (Input == EDBActionInput::Launcher)
	{
		return EDBAttackId::Launcher;
	}
	EDBAttackId Next;
	if (IsAttacking())
	{
		Next = Input == EDBActionInput::HeavyAttack ? ActiveAttack.NextHeavy : ActiveAttack.NextLight;
	}
	else if (CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->IsFalling())
	{
		Next = Input == EDBActionInput::HeavyAttack ? EDBAttackId::AirHeavy : EDBAttackId::AirLight1;
	}
	else
	{
		Next = Input == EDBActionInput::HeavyAttack ? EDBAttackId::Heavy1 : EDBAttackId::Light1;
	}
	// The existing chain can advance, but only landing permits another air-light opener.
	return Next == EDBAttackId::AirLight1 && bAirLightComboUsed ? EDBAttackId::None : Next;
}

bool UDBCombatComponent::IsAttackMovementValid(EDBAttackId Id) const
{
	const UCharacterMovementComponent* Movement = CastChecked<ACharacter>(GetOwner())->GetCharacterMovement();
	if (Id == EDBAttackId::Launcher)
	{
		return Movement->IsFalling() || Movement->IsMovingOnGround();
	}
	if (Id == EDBAttackId::AirHeavy)
	{
		return Movement->IsFalling() || (bAttackLanded && Movement->IsMovingOnGround());
	}
	return IsAirLight(Id) ? Movement->IsFalling() : Movement->IsMovingOnGround();
}

const FDBAttackDefinition* UDBCombatComponent::FindAttack(EDBAttackId Id) const
{
	if (!WeaponData)
	{
		return nullptr;
	}
	const FDBAttackDefinition* Found = nullptr;
	for (const FDBAttackDefinition& Definition : WeaponData->Attacks)
	{
		if (Definition.Id != Id)
		{
			continue;
		}
		if (Found)
		{
			return nullptr;
		} // Ambiguous authoring must not select an arbitrary move.
		Found = &Definition;
	}
	return Found;
}

bool UDBCombatComponent::CanJumpFromAction() const
{
	if (ActionState == EDBActionState::HitReaction)
	{
		return false;
	}
	if (IsAttacking())
	{
		if (ActiveAttack.Id == EDBAttackId::Launcher && bAttackMovementStarted
		    && CastChecked<ACharacter>(GetOwner())->JumpCurrentCount == 0)
		{
			// Let CMC count the launcher's first jump; later jump cancels use the authored gate.
			return true;
		}
		return PreviousAttackPosition >= ActiveAttack.JumpCancelStart;
	}
	return ActionState != EDBActionState::Evade
	       || GetWorld()->GetTimeSeconds() >= ActionStartedAt + EvadeJumpCancelStart;
}

void UDBCombatComponent::TryStartJump()
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	if (!Character->CanJump())
	{
		BufferedAction = {};
		return;
	}
	// Stop the ground evade before CMC enters Falling, preserving the separate air-evade budget.
	StopEvadeMovement(false);
	FinishAction(ActiveActionId);
	StopAirMovement();
	StopEvadeMontage();
	BufferedAction = {};
	Character->Jump();
}

bool UDBCombatComponent::TryTransitionToSprint()
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bLeavingBackward = bBackwardEvade
	                              && FVector::DotProduct(Character->GetPendingMovementInputVector().GetSafeNormal2D(),
	                                     Character->GetActorForwardVector())
	                                     > 0.0;
	const double HandoffTime =
	    bLeavingBackward ? InvulnerabilityEnd : FMath::Max(SprintTransitionStart + HeldSlideTime, InvulnerabilityEnd);
	if (!CanProcessInput() || !Movement->IsMovingOnGround() || ActionState != EDBActionState::Evade
	    || EvadeMotionId == 0 || BufferedAction.Id != 0 || GetWorld()->GetTimeSeconds() >= MovementEndsAt
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

void UDBCombatComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ParryHitStopEndsAt != 0.0 && FPlatformTime::Seconds() >= ParryHitStopEndsAt)
	{
		StopParryHitStop();
	}
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}
	if (ActionState == EDBActionState::HitReaction)
	{
		ACharacter* Character = CastChecked<ACharacter>(GetOwner());
		Character->ConsumeMovementInputVector();
		UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
		if (!Anim || !Anim->GetMontageInstanceForID(ReactionMontageInstanceId))
		{
			FinishAction(ActiveActionId);
		}
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (IsAttacking() && !IsAttackMovementValid(ActiveAttack.Id))
	{
		// A mode change cannot carry a hit window or a queued ground/air follow-up across it.
		FinishAction(ActiveActionId);
	}
	if (EvadeMotionId != 0 && CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->IsFalling())
	{
		// A ground evade carried off a ledge already occupies this airborne evade.
		bAirEvadeUsed = true;
	}
	// One deliberate follow-up survives the current action, rather than expiring during its wind-up.
	if (ActionState == EDBActionState::None && BufferedAction.IsExpired(Now, InputBufferLifetime))
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s expired #%llu at %.3f"), *GetOwner()->GetName(), BufferedAction.Id, Now);
		BufferedAction = {};
	}
	const bool bEvadeExit = ActionState == EDBActionState::Evade
	                        && ((BufferedAction.Input == EDBActionInput::Evade && Now >= ChainStartsAt)
	                            || (IsAttackInput(BufferedAction.Input)
	                                && Now >= ActionStartedAt + FMath::Max(EvadeAttackCancelStart, InvulnerabilityEnd))
	                            || (BufferedAction.Input == EDBActionInput::Jump && CanJumpFromAction()));
	const bool bAttackExit = IsAttacking()
	                         && (BufferedAction.Input == EDBActionInput::Launcher
	                             || (BufferedAction.Input == EDBActionInput::Evade
	                                 && PreviousAttackPosition >= ActiveAttack.EvadeCancelStart)
	                             || (BufferedAction.Input == EDBActionInput::Jump && CanJumpFromAction()));
	if (BufferedAction.Id != 0 && (bEvadeExit || bAttackExit || ActionState == EDBActionState::None))
	{
		if (BufferedAction.Input == EDBActionInput::Evade)
		{
			TryStartEvade(Now);
		}
		else if (BufferedAction.Input == EDBActionInput::Jump)
		{
			TryStartJump();
		}
		else
		{
			TryStartAttack();
		}
	}
	UpdateAttackMovement(DeltaTime);
	if (IsAttacking())
	{
		ACharacter* Character = CastChecked<ACharacter>(GetOwner());
		UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
		FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(AttackMontageInstanceId) : nullptr;
		if (!Instance && !bAttackEndedPending)
		{
			FinishAction(ActiveActionId);
		}
		else if (Character->GetCharacterMovement()->IsMovingOnGround() && BufferedAction.Id == 0
		         && PreviousAttackPosition >= ActiveAttack.MoveCancelStart
		         && !Character->GetPendingMovementInputVector().IsNearlyZero())
		{
			// Keep this frame's movement intent for CMC instead of consuming it during recovery.
			FinishAction(ActiveActionId);
		}
		else if (Character->GetCharacterMovement()->IsMovingOnGround())
		{
			// Hold ground movement during the committed part of a strike.
			Character->ConsumeMovementInputVector();
		}
	}
	if (ActionState == EDBActionState::Evade)
	{
		UpdateEvadeMovement(DeltaTime);
	}
	if (ActionState == EDBActionState::Evade && bBlendingToSprint && !bEvadeHeld && BufferedAction.Id == 0)
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
		if (!IsEvadeMontageActive()
		    || !CastChecked<ACharacter>(GetOwner())->GetPendingMovementInputVector().IsNearlyZero())
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
	const TSharedPtr<FRootMotionSource_ConstantForce> Motion =
	    StaticCastSharedPtr<FRootMotionSource_ConstantForce>(Movement->GetRootMotionSourceByID(EvadeMotionId));
	if (!Motion)
	{
		return;
	}

	const FVector Direction = Character->GetPendingMovementInputVector().GetSafeNormal2D();
	if (!Direction.IsNearlyZero()
	    && (!bBackwardEvade || FVector::DotProduct(Direction, Character->GetActorForwardVector()) <= 0.0))
	{
		EvadeDirection = Direction;
	}

	// Stretch only the glide, preserving push-off, recovery and the wall-clock i-frame interval.
	const double PoseTime = GetWorld()->GetTimeSeconds() - ActionStartedAt - HeldSlideTime;
	const double ExtraTime = bEvadeHeld && !Direction.IsNearlyZero() && BufferedAction.Id == 0 && PoseTime >= 0.18
	                                 && PoseTime < SprintTransitionStart
	                             ? FMath::Min(DeltaTime * 0.5, FMath::Max(0.0, MaxHeldSlideTime - HeldSlideTime))
	                             : 0.0;
	if (ExtraTime > 0.0)
	{
		// CMC still performs collision/gravity; slowing this finite source also slows its speed curve.
		Motion->SetTime(FMath::Max(0.0f, Motion->GetTime() - static_cast<float>(ExtraTime)));
		HeldSlideTime += ExtraTime;
		MovementEndsAt += ExtraTime;
		ChainStartsAt += ExtraTime;
	}
	UCurveFloat* Curve = bBackwardEvade && BackwardEvadeStrengthCurve ? BackwardEvadeStrengthCurve : EvadeStrengthCurve;
	float Speed =
	    EvadeSpeed
	    * (Curve ? Curve->GetFloatValue(FMath::Clamp(Motion->GetTime() / Motion->Duration, 0.0f, 1.0f)) : 1.0f);
	const bool bHeldForward = bEvadeHeld && !bBackwardEvade && !Direction.IsNearlyZero() && BufferedAction.Id == 0;
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
		BufferedAction = {};
		return;
	}
	FVector Direction = Character->GetPendingMovementInputVector().GetSafeNormal2D();
	const auto* Targeting = Character->FindComponentByClass<UDBTargetingComponent>();
	const bool bStartBackward = Direction.IsNearlyZero()
	                            || (Targeting && Targeting->IsLockedOn()
	                                && FVector::DotProduct(Direction, Character->GetActorForwardVector()) < -0.5);
	if (Direction.IsNearlyZero())
	{
		Direction = -Character->GetActorForwardVector().GetSafeNormal2D();
	}
	const double Duration = bStartBackward ? BackwardEvadeDuration : EvadeDuration;
	const double ChainStart = bStartBackward ? BackwardEvadeChainStart : EvadeChainStart;
	if (!FMath::IsFinite(Duration) || Duration <= 0.0 || Duration > MAX_flt || !FMath::IsFinite(EvadeSpeed)
	    || EvadeSpeed <= 0.0f || !FMath::IsFinite(InvulnerabilityStart) || !FMath::IsFinite(InvulnerabilityEnd)
	    || InvulnerabilityStart < 0.0 || InvulnerabilityEnd <= InvulnerabilityStart || !FMath::IsFinite(ChainStart)
	    || InvulnerabilityEnd >= ChainStart || ChainStart >= Duration)
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
	// New motion is valid. Finish the previous action before publishing this source's ID.
	StopEvadeMovement(false);
	FinishAction(ActiveActionId, false);
	StopAirMovement();
	EvadeMotionId = NewMotionId;
	bBackwardEvade = bStartBackward;
	EvadeDirection = Direction;
	bBlendingToSprint = false;
	if (Movement->IsFalling())
	{
		bAirEvadeUsed = true;
	}
	ActiveActionId = BufferedAction.Id;
	ActionStartedAt = Now;
	HeldSlideTime = 0.0;
	MovementEndsAt = Now + Duration;
	ChainStartsAt = Now + ChainStart;
	ActionState = EDBActionState::Evade;
	BufferedAction = {};
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
	UE_LOG(LogDBCombat, Log, TEXT("%s started Evade #%llu at %.3f, movement ends %.3f"), *GetOwner()->GetName(),
	    ActiveActionId, Now, MovementEndsAt);
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
	UE_LOG(LogDBCombat, Log, TEXT("%s finished action #%llu at %.3f"), *GetOwner()->GetName(), ActiveActionId,
	    GetWorld()->GetTimeSeconds());
	StopEvadeMovement();
	StopAttackMontage();
	StopHitReaction();
	if (bInterrupted)
	{
		StopEvadeMontage();
		BufferedAction = {};
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

void UDBCombatComponent::HandleLanding()
{
	StopAirMovement();
	ResetAirEvade();
	bAirLightComboUsed = false;
	GroundJumpPressedAt = -1.0;
	if (GetAttackId() == EDBAttackId::AirHeavy)
	{
		bAttackLanded = true;
		bAttackMovementStarted = true;
		if (UAnimInstance* Anim = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance())
		{
			Anim->Montage_SetPosition(ActiveAttack.Montage, FMath::Max(PreviousAttackPosition, 0.36f));
			Anim->Montage_Resume(ActiveAttack.Montage);
		}
	}
	else if (IsAirLight(GetAttackId()) || (GetAttackId() == EDBAttackId::Launcher && bAttackMovementStarted))
	{
		FinishAction(ActiveActionId);
	}
}

void UDBCombatComponent::StartAirMovement(float Duration)
{
	StopAirMovement();
	TSharedPtr<FRootMotionSource_ConstantForce> Motion = MakeShared<FRootMotionSource_ConstantForce>();
	Motion->InstanceName = TEXT("DBAirCombat");
	Motion->Priority = 90;
	Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
	Motion->Duration = Duration;
	Motion->Force = FVector::ZeroVector;
	Motion->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
	UCharacterMovementComponent* Movement = CastChecked<ACharacter>(GetOwner())->GetCharacterMovement();
	// PhysFalling integrates the old and new velocities; discard the entry lift as well.
	Movement->Velocity.Z = 0.0;
	AirMotionId = Movement->ApplyRootMotionSource(Motion);
}

void UDBCombatComponent::HoldAirborne(ACharacter* Attacker)
{
	StartAirMovement(0.75f);
	JuggleAttacker = Attacker;
	JuggleHeight = Attacker->GetActorLocation().Z;
}

void UDBCombatComponent::StopAirMovement()
{
	if (AirMotionId != 0)
	{
		CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->RemoveRootMotionSourceByID(AirMotionId);
		AirMotionId = 0;
	}
	JuggleAttacker.Reset();
}

void UDBCombatComponent::UpdateAttackMovement(float DeltaTime)
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const TSharedPtr<FRootMotionSource_ConstantForce> AirMotion = StaticCastSharedPtr<FRootMotionSource_ConstantForce>(
	    Character->GetCharacterMovement()->GetRootMotionSourceByID(AirMotionId));
	if (AirMotion)
	{
		if (JuggleAttacker.IsValid())
		{
			const UDBCombatComponent* AttackerCombat = JuggleAttacker->FindComponentByClass<UDBCombatComponent>();
			if (!AttackerCombat
			    || (!IsAirLight(AttackerCombat->GetAttackId())
			        && AttackerCombat->GetAttackId() != EDBAttackId::AirHeavy))
			{
				StopAirMovement();
			}
			else if (DeltaTime > 0.0f)
			{
				const double Error = JuggleHeight - Character->GetActorLocation().Z;
				const double Speed = Error * (1.0 - FMath::Exp(-8.0 * DeltaTime)) / DeltaTime;
				AirMotion->Force = FVector(0, 0, FMath::Clamp(Speed, -160.0, 160.0));
			}
		}
		else if (IsAirLight(GetAttackId()))
		{
			const float Push = FMath::SmoothStep(0.0f, 0.05f, PreviousAttackPosition)
			                   * (1.0f - FMath::SmoothStep(0.13f, 0.24f, PreviousAttackPosition));
			AirMotion->Force = Character->GetActorForwardVector() * (180.0f * Push);
		}
		else if (GetAttackId() != EDBAttackId::AirHeavy)
		{
			StopAirMovement();
		}
	}
	else
	{
		AirMotionId = 0;
		JuggleAttacker.Reset();
	}
	if (ActiveAttack.Id == EDBAttackId::Launcher)
	{
		if (!bAttackMovementStarted && PreviousAttackPosition >= 0.25f)
		{
			bAttackMovementStarted = true;
			if (Character->JumpCurrentCount == 0)
			{
				Character->Jump();
			}
			Character->LaunchCharacter(FVector(0, 0, 850), false, true);
		}
		else if (bAttackMovementStarted && Character->JumpCurrentCount > 0)
		{
			Character->StopJumping();
		}
	}
	else if (ActiveAttack.Id == EDBAttackId::AirHeavy && !bAttackMovementStarted && PreviousAttackPosition >= 0.30f)
	{
		bAttackMovementStarted = true;
		StopAirMovement();
		Character->LaunchCharacter(FVector(0, 0, -1800), false, true);
	}
}

void UDBCombatComponent::TryStartAttack()
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const FDBAttackDefinition* Definition = FindAttack(NextAttack(BufferedAction.Input));
	const bool bContinuing = ActionState != EDBActionState::None;
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Anim || !Definition || !Definition->IsValid() || !IsAttackMovementValid(Definition->Id) || !ConfigureBlade())
	{
		BufferedAction = {};
		return;
	}
	if (bContinuing)
	{
		StopEvadeMovement(false);
		FinishAction(ActiveActionId, false);
	}
	if (Anim->Montage_Play(Definition->Montage, Definition->PlayRate) <= 0.0f)
	{
		BufferedAction = {};
		return;
	}
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Definition->Montage);
	check(Instance);
	ActiveAttack = *Definition;
	if (ActiveAttack.Id == EDBAttackId::AirLight1)
	{
		bAirLightComboUsed = true;
	}
	ActiveHitWindow = INDEX_NONE;
	AttackMontageInstanceId = Instance->GetInstanceID();
	PreviousAttackPosition = 0.0f;
	HitActors.Reset();
	bAttackEndedPending = false;
	bAttackMovementStarted = false;
	bAttackLanded = false;
	bParryUsed = false;
	ActionStartedAt = GetWorld()->GetTimeSeconds();
	ActiveActionId = BufferedAction.Id;
	ActionState =
	    BufferedAction.Input == EDBActionInput::LightAttack ? EDBActionState::LightAttack : EDBActionState::HeavyAttack;
	BufferedAction = {};
	Character->StopJumping();
	if (!bContinuing && Character->GetCharacterMovement()->IsMovingOnGround())
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
	if (ActiveAttack.Id == EDBAttackId::Launcher)
	{
		// A slightly earlier jump must not lift the startup swing above a grounded target.
		Character->GetCharacterMovement()->Velocity.Z = FMath::Min(Character->GetVelocity().Z, 0.0);
		GroundJumpPressedAt = -1.0;
	}
	Instance->OnMontageBlendingOutStarted.BindUObject(this, &ThisClass::AttackBlendingOut, ActiveActionId);
	Instance->OnMontageEnded.BindUObject(this, &ThisClass::AttackEnded, ActiveActionId);
	OnAttackStarted.Broadcast();
	if (IsAirLight(ActiveAttack.Id) || ActiveAttack.Id == EDBAttackId::AirHeavy)
	{
		StartAirMovement(ActiveAttack.Montage->GetPlayLength() / ActiveAttack.PlayRate);
	}
	PreviousMeshTransform = Character->GetMesh()->GetComponentTransform();
	PreviousWeaponTransform = GetWeaponTransform();
	EarlierWeaponTransform = PreviousWeaponTransform;
	WeaponPoseTime = EarlierWeaponTime = ActionStartedAt;
}

void UDBCombatComponent::SampleAttackWindow(float Position)
{
	const uint64 ActionId = ActiveActionId;
	const float Previous = PreviousAttackPosition;
	PreviousAttackPosition = Position;
	for (const FDBAttackWindow& Window : ActiveAttack.HitWindows)
	{
		if (Window.Overlaps(Previous, Position))
		{
			OnAttackWindowTraversed.Broadcast(FMath::Max(Previous, Window.Start), FMath::Min(Position, Window.End));
			if (ActionId != ActiveActionId)
			{
				return;
			}
		}
	}
}

bool UDBCombatComponent::IsAttackWindowOpen() const
{
	if (!IsAttacking())
	{
		return false;
	}
	for (const FDBAttackWindow& Window : ActiveAttack.HitWindows)
	{
		if (PreviousAttackPosition >= Window.Start && PreviousAttackPosition < Window.End)
		{
			return true;
		}
	}
	return false;
}

void UDBCombatComponent::AttackBlendingOut(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId)
{
	if (ActionId != ActiveActionId || !IsAttacking())
	{
		return;
	}
	if (bInterrupted)
	{
		FinishAction(ActionId);
		return;
	}
	// Natural blend-out still has an instance. BonesFinalized samples its fresh pose.
}

void UDBCombatComponent::AttackEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId)
{
	if (ActionId != ActiveActionId || !IsAttacking())
	{
		return;
	}
	if (bInterrupted)
	{
		FinishAction(ActionId);
	}
	else
	{
		bAttackEndedPending = true;
	} // Flush the final interval after this frame's bone evaluation.
}

void UDBCombatComponent::StopAttackMontage()
{
	StopAirMovement();
	if (AttackMontageInstanceId == INDEX_NONE)
	{
		return;
	}
	UAnimInstance* Anim = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(AttackMontageInstanceId) : nullptr;
	AttackMontageInstanceId = INDEX_NONE;
	ActiveAttack = {};
	PreviousAttackPosition = 0.0f;
	bAttackEndedPending = false;
	ActiveHitWindow = INDEX_NONE;
	HitActors.Reset();
	if (Instance)
	{
		Instance->OnMontageBlendingOutStarted.Unbind();
		Instance->OnMontageEnded.Unbind();
		// An interrupted pose may fade, but its attack displacement must stop now.
		Instance->PushDisableRootMotion();
		Instance->Stop(FAlphaBlend(0.10f), true);
	}
}

void UDBCombatComponent::ReactToHit(UAnimMontage* Montage)
{
	ResetCombat();
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const double FallingSpeed = Movement->IsFalling() ? Movement->Velocity.Z : 0.0;
	Movement->StopMovementImmediately();
	Movement->Velocity.Z = FallingSpeed;
	Character->ConsumeMovementInputVector();
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Montage || !Anim || Anim->Montage_Play(Montage) <= 0.0f)
	{
		return;
	}
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	check(Instance);
	ReactionMontageInstanceId = Instance->GetInstanceID();
	ActiveActionId = ++NextRequestId;
	ActionStartedAt = GetWorld()->GetTimeSeconds();
	ActionState = EDBActionState::HitReaction;
	Instance->OnMontageBlendingOutStarted.BindUObject(this, &ThisClass::HitReactionBlendingOut, ActiveActionId);
	Instance->OnMontageEnded.BindUObject(this, &ThisClass::HitReactionEnded, ActiveActionId);
}

void UDBCombatComponent::HitReactionBlendingOut(UAnimMontage*, bool bInterrupted, uint64 ActionId)
{
	if (bInterrupted)
	{
		FinishAction(ActionId);
	}
}

void UDBCombatComponent::HitReactionEnded(UAnimMontage*, bool bInterrupted, uint64 ActionId)
{
	FinishAction(ActionId, bInterrupted);
}

void UDBCombatComponent::StopHitReaction()
{
	if (ReactionMontageInstanceId == INDEX_NONE)
	{
		return;
	}
	UAnimInstance* Anim = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(ReactionMontageInstanceId) : nullptr;
	ReactionMontageInstanceId = INDEX_NONE;
	if (Instance)
	{
		Instance->OnMontageBlendingOutStarted.Unbind();
		Instance->OnMontageEnded.Unbind();
		Instance->Stop(FAlphaBlend(0.08f), true);
	}
}

bool UDBCombatComponent::ConfigureBlade()
{
	const UStaticMesh* Mesh = WeaponMesh ? WeaponMesh->GetStaticMesh() : nullptr;
	const UStaticMeshSocket* Base = Mesh ? Mesh->FindSocket(TEXT("BladeBase")) : nullptr;
	const UStaticMeshSocket* Tip = Mesh ? Mesh->FindSocket(TEXT("BladeTip")) : nullptr;
	if (!WeaponData || !Base || !Tip || !FMath::IsFinite(WeaponData->BladeTraceRadius)
	    || WeaponData->BladeTraceRadius < 0.5f)
	{
		UE_LOG(LogDBCombat, Warning,
		    TEXT("Assign a weapon with BladeBase/BladeTip sockets and a valid trace radius on %s."),
		    *GetOwner()->GetName());
		return false;
	}
	BladeBaseLocal = Base->RelativeLocation;
	BladeTipLocal = Tip->RelativeLocation;
	return FVector::DistSquared(BladeBaseLocal, BladeTipLocal) > 1.0;
}

FTransform UDBCombatComponent::GetWeaponTransform() const
{
	const USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	// Attached components may update later than the bone-finalization callback.
	return WeaponMesh->GetRelativeTransform() * Mesh->GetSocketTransform(WeaponMesh->GetAttachSocketName());
}

FTransform UDBCombatComponent::SampleWeaponTransform(
    float Position, float CurrentPosition, const FTransform& CurrentMesh, const FTransform& CurrentWeapon) const
{
	if (FMath::IsNearlyEqual(Position, PreviousAttackPosition))
	{
		return PreviousWeaponTransform;
	}
	if (FMath::IsNearlyEqual(Position, CurrentPosition) && !bAttackEndedPending)
	{
		return CurrentWeapon;
	}

	// A hitch may cross the entire hit window. Recover the authored poses inside
	// that interval instead of tracing the later recovery pose or a single chord.
	USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	const FBoneContainer& Bones = Mesh->GetAnimInstance()->GetRequiredBones();
	const USkeletalMeshSocket* Socket = Mesh->GetSocketByName(WeaponMesh->GetAttachSocketName());
	const FCompactPoseBoneIndex Bone = Bones.MakeCompactPoseIndex(
	    FMeshPoseBoneIndex(Mesh->GetBoneIndex(Socket ? Socket->BoneName : WeaponMesh->GetAttachSocketName())));
	const FAnimTrack* Track = ActiveAttack.Montage->GetAnimationData(TEXT("DefaultSlot"));
	if (!Track || Bone == INDEX_NONE)
	{
		return CurrentWeapon;
	}

	FMemMark Mark(FMemStack::Get());
	FCompactPose Pose;
	Pose.SetBoneContainer(&Bones);
	Pose.ResetToRefPose();
	FBlendedCurve Curve;
	Curve.InitFrom(Bones);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData Data(Pose, Curve, Attributes);
	Track->GetAnimationPose(Data, FAnimExtractContext(Position, true));
	FCSPose<FCompactPose> ComponentPose;
	ComponentPose.InitPose(Pose);
	FTransform MeshAtTime;
	const float Alpha =
	    FMath::Clamp((Position - PreviousAttackPosition) / (CurrentPosition - PreviousAttackPosition), 0.0f, 1.0f);
	MeshAtTime.Blend(PreviousMeshTransform, CurrentMesh, Alpha);
	const FTransform SocketLocal = Socket ? Socket->GetSocketLocalTransform() : FTransform::Identity;
	return WeaponMesh->GetRelativeTransform() * SocketLocal * ComponentPose.GetComponentSpaceTransform(Bone)
	       * MeshAtTime;
}

void UDBCombatComponent::BonesFinalized()
{
	if (!IsAttacking() || !CanProcessInput())
	{
		return;
	}
	if (!IsAttackMovementValid(ActiveAttack.Id))
	{
		// CMC may leave a ledge or land after the combat tick but before this pose is finalized.
		FinishAction(ActiveActionId);
		return;
	}
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	const FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(AttackMontageInstanceId) : nullptr;
	if (!Instance && !bAttackEndedPending)
	{
		return;
	}
	const uint64 ActionId = ActiveActionId;
	float Position = bAttackEndedPending ? ActiveAttack.Montage->GetPlayLength() : Instance->GetPosition();
	if (ActiveAttack.Id == EDBAttackId::AirHeavy && !bAttackLanded && Position >= 0.32f)
	{
		// Hold the falling pose; recovery resumes from HandleLanding, never by forcing Walking.
		Position = 0.32f;
		Anim->Montage_SetPosition(ActiveAttack.Montage, Position);
		Anim->Montage_Pause(ActiveAttack.Montage);
	}
	const FTransform CurrentMesh = Character->GetMesh()->GetComponentTransform();
	const FTransform CurrentWeapon = GetWeaponTransform();
	TraceAttack(PreviousAttackPosition, Position, CurrentMesh, CurrentWeapon);
	if (ActionId != ActiveActionId)
	{
		return;
	} // Damage callbacks may interrupt or kill the attacker.
	SampleAttackWindow(Position);
	if (ActionId != ActiveActionId)
	{
		return;
	}
	PreviousMeshTransform = CurrentMesh;
	EarlierWeaponTransform = PreviousWeaponTransform;
	EarlierWeaponTime = WeaponPoseTime;
	PreviousWeaponTransform = CurrentWeapon;
	WeaponPoseTime = GetWorld()->GetTimeSeconds();
	// Flush this frame's contacts before changing the montage or resetting its per-swing hit set.
	if (BufferedAction.Id != 0 && IsAttackInput(BufferedAction.Input) && Position >= ActiveAttack.ComboStart)
	{
		TryStartAttack();
		if (ActionId != ActiveActionId)
		{
			return;
		}
	}
	if (bAttackEndedPending)
	{
		FinishAction(ActionId, false);
	}
}

void UDBCombatComponent::TraceAttack(
    float Previous, float Current, const FTransform& CurrentMesh, const FTransform& CurrentWeapon)
{
	const uint64 ActionId = ActiveActionId;
	const double Now = GetWorld()->GetTimeSeconds();
	if (ActiveAttack.Id == EDBAttackId::AirHeavy && FMath::IsNearlyEqual(Previous, Current) && IsAttackWindowOpen())
	{
		// Montage time is held, but CMC is still moving the blade through the world.
		if (!SweepParry(
		        PreviousWeaponTransform, CurrentWeapon, Now - GetWorld()->GetDeltaSeconds(), Now, ActiveHitWindow))
		{
			SweepBlade(PreviousWeaponTransform, CurrentWeapon, ActionId);
		}
		return;
	}
	for (int32 Index = 0; Index < ActiveAttack.HitWindows.Num() && ActiveActionId == ActionId; ++Index)
	{
		const FDBAttackWindow Window = ActiveAttack.HitWindows[Index];
		// A weapon clash may catch the final 0.10 seconds of preparation; damage still starts at Window.Start.
		const double Rate = ActiveAttack.PlayRate * ActiveAttack.Montage->RateScale;
		const float ClashStart = FMath::Max(0.0f, Window.Start - 0.10f * Rate);
		if (!FDBAttackWindow{ClashStart, Window.End}.Overlaps(Previous, Current))
		{
			continue;
		}
		const float From = FMath::Max(Previous, ClashStart);
		const float To = FMath::Min(Current, Window.End);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt((To - From) * 120.0f), 1, 32);
		FTransform Last = SampleWeaponTransform(From, Current, CurrentMesh, CurrentWeapon);
		double LastTime = Now - (Current - From) / Rate;
		float LastPosition = From;
		for (int32 Step = 1; Step <= Steps && ActiveActionId == ActionId; ++Step)
		{
			const float Time = FMath::Lerp(From, To, static_cast<float>(Step) / Steps);
			const FTransform Next = SampleWeaponTransform(Time, Current, CurrentMesh, CurrentWeapon);
			const double NextTime = Now - (Current - Time) / Rate;
			if (SweepParry(Last, Next, LastTime, NextTime, Index))
			{
				return;
			}
			if (Time > Window.Start)
			{
				if (ActiveHitWindow != Index)
				{
					HitActors.Reset();
					ActiveHitWindow = Index;
				}
				const float DamageStart = FMath::Max(LastPosition, Window.Start);
				const FTransform DamageFrom = DamageStart == LastPosition ? Last
				                                                          : SampleWeaponTransform(DamageStart, Current,
				                                                                CurrentMesh, CurrentWeapon);
				SweepBlade(DamageFrom, Next, ActionId);
			}
			Last = Next;
			LastTime = NextTime;
			LastPosition = Time;
		}
	}
}

bool UDBCombatComponent::HasAttackStaggerResistance() const
{
	if (!IsAttacking() || !CanProcessInput()
	    || !CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}
	// Use the action clock; the opponent's pose may finalize earlier in this frame.
	const double Position =
	    (GetWorld()->GetTimeSeconds() - ActionStartedAt) * ActiveAttack.PlayRate * ActiveAttack.Montage->RateScale;
	return Position >= ActiveAttack.StaggerResistanceWindow.Start
	       && Position < ActiveAttack.StaggerResistanceWindow.End;
}

bool UDBCombatComponent::SweepParry(
    const FTransform& From, const FTransform& To, double FromTime, double ToTime, int32 HitWindow)
{
	const FVector FromBase = From.TransformPosition(BladeBaseLocal);
	const FVector FromTip = From.TransformPosition(BladeTipLocal);
	const FVector ToBase = To.TransformPosition(BladeBaseLocal);
	const FVector ToTip = To.TransformPosition(BladeTipLocal);
	// Broad phase only: a candidate's hand and sword can extend beyond its body capsule.
	const float Radius =
	    200.0f + FMath::Max(FVector::Distance(FromBase, FromTip), FVector::Distance(ToBase, ToTip)) * 0.5f;
	const FVector FromCenter = (FromBase + FromTip) * 0.5f;
	const FVector ToCenter = (ToBase + ToTip) * 0.5f;
	TArray<FOverlapResult> Candidates;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBParry), false, GetOwner());
	GetWorld()->OverlapMultiByObjectType(Candidates, (FromCenter + ToCenter) * 0.5f, FQuat::Identity,
	    FCollisionObjectQueryParams(ECC_Pawn),
	    FCollisionShape::MakeSphere(Radius + FVector::Distance(FromCenter, ToCenter) * 0.5f), Params);
	const auto* Team = Cast<IGenericTeamAgentInterface>(CastChecked<ACharacter>(GetOwner())->GetController());
	for (const FOverlapResult& Candidate : Candidates)
	{
		AActor* Target = Candidate.GetActor();
		if (!IsValid(Target) || (ActiveHitWindow == HitWindow && HitActors.Contains(Target))
		    || (Team && Team->GetTeamAttitudeTowards(*Target) != ETeamAttitude::Hostile))
		{
			continue;
		}
		UDBCombatComponent* Defender = Target->FindComponentByClass<UDBCombatComponent>();
		if (!Defender || !Defender->IsAttacking() || Defender->ActiveAttack.ParryWindow.End <= 0)
		{
			continue;
		}
		for (float Alpha : {0.0f, 0.5f, 1.0f})
		{
			if (Defender->TryParry(*this, FMath::Lerp(FromTime, ToTime, Alpha), FMath::Lerp(FromBase, ToBase, Alpha),
			        FMath::Lerp(FromTip, ToTip, Alpha)))
			{
				return true;
			}
		}
	}
	return false;
}

bool UDBCombatComponent::TryParry(
    UDBCombatComponent& Attacker, double ContactTime, const FVector& BladeBase, const FVector& BladeTip)
{
	if (!WeaponData || !Attacker.WeaponData || !IsAttacking() || bParryUsed || !CanProcessInput()
	    || !GetOwner()->CanBeDamaged() || !IsAttackMovementValid(ActiveAttack.Id))
	{
		return false;
	}
	// Evaluate the swept contact's time, independent of the two characters' pose tick order.
	const double Position = (ContactTime - ActionStartedAt) * ActiveAttack.PlayRate * ActiveAttack.Montage->RateScale;
	const FVector ToAttacker =
	    (Attacker.GetOwner()->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal2D();
	if (Position < ActiveAttack.ParryWindow.Start || Position >= ActiveAttack.ParryWindow.End
	    || FVector::DotProduct(GetOwner()->GetActorForwardVector(), ToAttacker) < 0.5)
	{
		return false;
	}
	FTransform Weapon = GetWeaponTransform();
	if (WeaponPoseTime > EarlierWeaponTime)
	{
		// Compare both blades at the swept sample's time, rather than mixing two animation frames.
		const float Alpha =
		    FMath::Clamp((ContactTime - EarlierWeaponTime) / (WeaponPoseTime - EarlierWeaponTime), 0.0, 1.0);
		Weapon.Blend(EarlierWeaponTransform, PreviousWeaponTransform, Alpha);
	}
	FVector IncomingPoint, DefenderPoint;
	FMath::SegmentDistToSegmentSafe(BladeBase, BladeTip, Weapon.TransformPosition(BladeBaseLocal),
	    Weapon.TransformPosition(BladeTipLocal), IncomingPoint, DefenderPoint);
	const float Separation = ParryBladeTolerance + WeaponData->BladeTraceRadius + Attacker.WeaponData->BladeTraceRadius;
	if (FVector::DistSquared(IncomingPoint, DefenderPoint) > FMath::Square(Separation))
	{
		return false;
	}
	const FVector ImpactPoint = (IncomingPoint + DefenderPoint) * 0.5f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBParrySight), false, GetOwner());
	Params.AddIgnoredActor(Attacker.GetOwner());
	FHitResult Obstruction;
	if (GetWorld()->LineTraceSingleByChannel(
	        Obstruction, GetOwner()->GetActorLocation(), ImpactPoint, DBCollision::Melee, Params)
	    || GetWorld()->LineTraceSingleByChannel(
	        Obstruction, Attacker.GetOwner()->GetActorLocation(), ImpactPoint, DBCollision::Melee, Params))
	{
		return false;
	}
	bParryUsed = true;
	if (ParryEffect)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ParryEffect, ImpactPoint);
	}
	if (ParrySound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ParrySound, ImpactPoint);
	}
	if (ParryHitStopDuration > 0.0f)
	{
		TimeDilationBeforeParry = UGameplayStatics::GetGlobalTimeDilation(this);
		UGameplayStatics::SetGlobalTimeDilation(this, FMath::Min(TimeDilationBeforeParry, 0.05f));
		ParryHitStopEndsAt = FPlatformTime::Seconds() + ParryHitStopDuration;
	}
	const uint64 AttackerActionId = Attacker.ActiveActionId;
	Attacker.OnParried.Broadcast(GetOwner());
	if (Attacker.ActiveActionId == AttackerActionId)
	{
		Attacker.ResetCombat();
	}
	return true;
}

void UDBCombatComponent::StopParryHitStop()
{
	if (ParryHitStopEndsAt != 0.0)
	{
		ParryHitStopEndsAt = 0.0;
		if (FMath::IsNearlyEqual(
		        UGameplayStatics::GetGlobalTimeDilation(this), FMath::Min(TimeDilationBeforeParry, 0.05f)))
		{
			UGameplayStatics::SetGlobalTimeDilation(this, TimeDilationBeforeParry);
		}
	}
}

void UDBCombatComponent::SweepBlade(const FTransform& From, const FTransform& To, uint64 ActionId)
{
	const double Length = FMath::Max(From.TransformVector(BladeTipLocal - BladeBaseLocal).Size(),
	    To.TransformVector(BladeTipLocal - BladeBaseLocal).Size());
	const int32 Samples = FMath::Clamp(FMath::CeilToInt(Length / (WeaponData->BladeTraceRadius * 2.0f)), 1, 64);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBBlade), false, GetOwner());
	TArray<FHitResult> Hits;
	for (int32 Sample = 0; Sample <= Samples && ActiveActionId == ActionId; ++Sample)
	{
		const FVector Local = FMath::Lerp(BladeBaseLocal, BladeTipLocal, static_cast<float>(Sample) / Samples);
		Hits.Reset();
		GetWorld()->SweepMultiByChannel(Hits, From.TransformPosition(Local), To.TransformPosition(Local),
		    FQuat::Identity, DBCollision::Melee, FCollisionShape::MakeSphere(WeaponData->BladeTraceRadius), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Target = Hit.GetActor();
			if (ActiveActionId != ActionId)
			{
				return;
			}
			if (!IsValid(Target) || HitActors.Contains(Target))
			{
				continue;
			}
			const auto* Team = Cast<IGenericTeamAgentInterface>(CastChecked<ACharacter>(GetOwner())->GetController());
			if (Team && Team->GetTeamAttitudeTowards(*Target) != ETeamAttitude::Hostile)
			{
				continue;
			}
			const UDBHealthComponent* Health = Target->FindComponentByClass<UDBHealthComponent>();
			if (!Health || Health->IsDead())
			{
				continue;
			}
			FCollisionQueryParams Sight = Params;
			Sight.AddIgnoredActor(Target);
			FHitResult Obstruction;
			if (GetWorld()->LineTraceSingleByChannel(
			        Obstruction, GetOwner()->GetActorLocation(), Hit.ImpactPoint, DBCollision::Melee, Sight))
			{
				continue;
			}
			// One contact attempt per actor/window, even when that target rejects it via i-frames.
			HitActors.Add(Target);
			UDBCombatComponent* VictimCombat = Target->FindComponentByClass<UDBCombatComponent>();
			const bool bResistsLaunch = VictimCombat && VictimCombat->HasAttackStaggerResistance();
			const EDBAttackId Attack = ActiveAttack.Id;
			const float Applied = UGameplayStatics::ApplyPointDamage(Target, ActiveAttack.Damage,
			    (To.TransformPosition(Local) - From.TransformPosition(Local)).GetSafeNormal(), Hit,
			    CastChecked<ACharacter>(GetOwner())->GetController(), GetOwner(), nullptr);
			ACharacter* Victim = Cast<ACharacter>(Target);
			if (Applied > 0.0f && IsValid(Victim) && !Health->IsDead() && ActiveActionId == ActionId && !bResistsLaunch)
			{
				if (Attack == EDBAttackId::Launcher)
				{
					Victim->LaunchCharacter(FVector(0, 0, 850), true, true);
				}
				else if (Victim->GetCharacterMovement()->IsFalling())
				{
					if (Attack == EDBAttackId::AirHeavy)
					{
						Victim->LaunchCharacter(FVector(0, 0, -1800), true, true);
					}
					else if (IsAirLight(Attack))
					{
						if (VictimCombat)
						{
							VictimCombat->HoldAirborne(CastChecked<ACharacter>(GetOwner()));
						}
					}
				}
			}
		}
	}
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
	StopParryHitStop();
	if (ActiveActionId != 0 || BufferedAction.Id != 0)
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s reset active #%llu / pending #%llu"), *GetOwner()->GetName(),
		    ActiveActionId, BufferedAction.Id);
	}
	FinishAction(ActiveActionId);
	StopAirMovement();
	// Reset also stops any residual blend-out after an interruption.
	StopEvadeMontage();
	BufferedAction = {};
	bEvadeHeld = false;
	GroundJumpPressedAt = -1.0;
}

void UDBCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(PoseFinalizedHandle);
	}
	ResetCombat();
	Super::EndPlay(EndPlayReason);
}
