#include "Combat/DBCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/CoreDelegates.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBCombat, Log, All);

bool FDBAttackDefinition::IsValid() const
{
	return Montage && FMath::IsFinite(PlayRate) && PlayRate > 0.0f
		&& FMath::IsFinite(ActiveStart) && FMath::IsFinite(ActiveEnd)
		&& ActiveStart >= 0.0f && ActiveEnd > ActiveStart && ActiveEnd < Montage->GetPlayLength()
		&& FMath::IsFinite(Damage) && Damage > 0.0f;
}

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
	USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	PoseFinalizedHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
		FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ThisClass::BonesFinalized));
}

bool UDBCombatComponent::CanProcessInput() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UDBHealthComponent* Health = GetOwner()->FindComponentByClass<UDBHealthComponent>();
	if (Health && Health->IsDead()) { return false; }
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
		BufferedAction = {};
		return;
	}
	BufferedAction = {++NextRequestId, GetWorld()->GetTimeSeconds(), EDBActionState::Evade};
	UE_LOG(LogDBCombat, Verbose, TEXT("%s queued #%llu at %.3f"), *GetOwner()->GetName(),
		BufferedAction.Id, BufferedAction.ReceivedAt);
}

void UDBCombatComponent::RequestAttack(bool bHeavy)
{
	if (!CanProcessInput()) { ResetCombat(); return; }
	// Air attacks join later. A rejected press must never replay on landing.
	if (!CastChecked<ACharacter>(GetOwner())->GetCharacterMovement()->IsMovingOnGround()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (BufferedAction.Action == EDBActionState::Evade && BufferedAction.Id != 0
		&& (ActionState == EDBActionState::Evade || !BufferedAction.IsExpired(Now, InputBufferLifetime))) { return; }
	BufferedAction = {++NextRequestId, Now, bHeavy ? EDBActionState::HeavyAttack : EDBActionState::LightAttack};
}

bool UDBCombatComponent::TryTransitionToSprint()
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bLeavingBackward = bBackwardEvade && FVector::DotProduct(
		Character->GetPendingMovementInputVector().GetSafeNormal2D(), Character->GetActorForwardVector()) > 0.0;
	const double HandoffTime = bLeavingBackward ? InvulnerabilityEnd : FMath::Max(SprintTransitionStart + HeldSlideTime, InvulnerabilityEnd);
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
	if (!(ActionState == EDBActionState::Evade && BufferedAction.Action == EDBActionState::Evade)
		&& BufferedAction.IsExpired(Now, InputBufferLifetime))
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s expired #%llu at %.3f"), *GetOwner()->GetName(), BufferedAction.Id, Now);
		BufferedAction = {};
	}
	const bool bCanChain = ActionState == EDBActionState::Evade && BufferedAction.Action == EDBActionState::Evade && Now >= ChainStartsAt;
	if (BufferedAction.Id != 0 && (bCanChain || (ActionState == EDBActionState::None && !IsEvadeMontageActive())))
	{
		// Start at the authored recovery gate, keeping its remaining momentum.
		if (BufferedAction.Action == EDBActionState::Evade) { TryStartEvade(Now); }
		else { TryStartAttack(); }
	}
	if (IsAttacking())
	{
		ACharacter* Character = CastChecked<ACharacter>(GetOwner());
		UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
		FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(AttackMontageInstanceId) : nullptr;
		if (!Character->GetCharacterMovement()->IsMovingOnGround() || (!Instance && !bAttackEndedPending)) { FinishAction(ActiveActionId); }
		else
		{
			// Root-motion animation owns translation and facing until recovery ends.
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
	const double ExtraTime = bEvadeHeld && !Direction.IsNearlyZero() && BufferedAction.Id == 0
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
	UE_LOG(LogDBCombat, Log, TEXT("%s finished action #%llu at %.3f"),
		*GetOwner()->GetName(), ActiveActionId, GetWorld()->GetTimeSeconds());
	StopEvadeMovement();
	StopAttackMontage();
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

void UDBCombatComponent::TryStartAttack()
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const FDBAttackDefinition& Definition = BufferedAction.Action == EDBActionState::HeavyAttack ? HeavyAttack : LightAttack;
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Character->GetCharacterMovement()->IsMovingOnGround() || !Anim || !Definition.IsValid() || !ConfigureBlade())
	{
		BufferedAction = {};
		return;
	}
	if (Anim->Montage_Play(Definition.Montage, Definition.PlayRate) <= 0.0f)
	{
		BufferedAction = {};
		return;
	}
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Definition.Montage);
	check(Instance);
	ActiveAttack = Definition;
	AttackMontageInstanceId = Instance->GetInstanceID();
	PreviousAttackPosition = 0.0f;
	PreviousMeshTransform = Character->GetMesh()->GetComponentTransform();
	PreviousWeaponTransform = GetWeaponTransform();
	HitActors.Reset();
	bAttackEndedPending = false;
	ActiveActionId = BufferedAction.Id;
	ActionState = BufferedAction.Action;
	BufferedAction = {};
	Character->StopJumping();
	Character->GetCharacterMovement()->StopMovementImmediately();
	Instance->OnMontageBlendingOutStarted.BindUObject(this, &ThisClass::AttackBlendingOut, ActiveActionId);
	Instance->OnMontageEnded.BindUObject(this, &ThisClass::AttackEnded, ActiveActionId);
}

void UDBCombatComponent::SampleAttackWindow(float Position)
{
	if (ActiveAttack.OverlapsActiveWindow(PreviousAttackPosition, Position))
	{
		const float From = FMath::Max(PreviousAttackPosition, ActiveAttack.ActiveStart);
		const float To = FMath::Min(Position, ActiveAttack.ActiveEnd);
		PreviousAttackPosition = Position;
		OnAttackWindowTraversed.Broadcast(From, To);
	}
	else { PreviousAttackPosition = Position; }
}

bool UDBCombatComponent::IsAttackWindowOpen() const
{
	return IsAttacking() && PreviousAttackPosition >= ActiveAttack.ActiveStart && PreviousAttackPosition < ActiveAttack.ActiveEnd;
}

void UDBCombatComponent::AttackBlendingOut(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId)
{
	if (ActionId != ActiveActionId || !IsAttacking()) { return; }
	if (bInterrupted) { FinishAction(ActionId); return; }
	// Natural blend-out still has an instance. BonesFinalized samples its fresh pose.
}

void UDBCombatComponent::AttackEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ActionId)
{
	if (ActionId != ActiveActionId || !IsAttacking()) { return; }
	if (bInterrupted) { FinishAction(ActionId); }
	else { bAttackEndedPending = true; } // Flush the final interval after this frame's bone evaluation.
}

void UDBCombatComponent::StopAttackMontage()
{
	if (AttackMontageInstanceId == INDEX_NONE) { return; }
	UAnimInstance* Anim = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(AttackMontageInstanceId) : nullptr;
	AttackMontageInstanceId = INDEX_NONE;
	ActiveAttack = {};
	PreviousAttackPosition = 0.0f;
	bAttackEndedPending = false;
	HitActors.Reset();
	if (Instance)
	{
		Instance->OnMontageBlendingOutStarted.Unbind();
		Instance->OnMontageEnded.Unbind();
		// An interrupted pose may fade, but its attack displacement must stop now.
		Instance->PushDisableRootMotion();
		if (Instance->IsPlaying()) { Instance->Stop(FAlphaBlend(0.10f), true); }
	}
}

bool UDBCombatComponent::ConfigureBlade()
{
	const UStaticMesh* Mesh = WeaponMesh ? WeaponMesh->GetStaticMesh() : nullptr;
	const UStaticMeshSocket* Base = Mesh ? Mesh->FindSocket(TEXT("BladeBase")) : nullptr;
	const UStaticMeshSocket* Tip = Mesh ? Mesh->FindSocket(TEXT("BladeTip")) : nullptr;
	if (!Base || !Tip || !FMath::IsFinite(BladeTraceRadius) || BladeTraceRadius < 0.5f)
	{
		UE_LOG(LogDBCombat, Warning, TEXT("Assign a weapon with BladeBase/BladeTip sockets and a valid trace radius on %s."), *GetOwner()->GetName());
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

FTransform UDBCombatComponent::SampleWeaponTransform(float Position, float CurrentPosition,
	const FTransform& CurrentMesh, const FTransform& CurrentWeapon) const
{
	if (FMath::IsNearlyEqual(Position, PreviousAttackPosition)) { return PreviousWeaponTransform; }
	if (FMath::IsNearlyEqual(Position, CurrentPosition) && !bAttackEndedPending) { return CurrentWeapon; }

	// A hitch may cross the entire hit window. Recover the authored poses inside
	// that interval instead of tracing the later recovery pose or a single chord.
	USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	const FBoneContainer& Bones = Mesh->GetAnimInstance()->GetRequiredBones();
	const USkeletalMeshSocket* Socket = Mesh->GetSocketByName(WeaponMesh->GetAttachSocketName());
	const FCompactPoseBoneIndex Bone = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(
		Mesh->GetBoneIndex(Socket ? Socket->BoneName : WeaponMesh->GetAttachSocketName())));
	const FAnimTrack* Track = ActiveAttack.Montage->GetAnimationData(TEXT("DefaultSlot"));
	if (!Track || Bone == INDEX_NONE) { return CurrentWeapon; }

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
	const float Alpha = FMath::Clamp((Position - PreviousAttackPosition) / (CurrentPosition - PreviousAttackPosition), 0.0f, 1.0f);
	MeshAtTime.Blend(PreviousMeshTransform, CurrentMesh, Alpha);
	const FTransform SocketLocal = Socket ? Socket->GetSocketLocalTransform() : FTransform::Identity;
	return WeaponMesh->GetRelativeTransform() * SocketLocal * ComponentPose.GetComponentSpaceTransform(Bone) * MeshAtTime;
}

void UDBCombatComponent::BonesFinalized()
{
	if (!IsAttacking() || !CanProcessInput()) { return; }
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	const FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(AttackMontageInstanceId) : nullptr;
	if (!Instance && !bAttackEndedPending) { return; }
	const uint64 ActionId = ActiveActionId;
	const float Position = bAttackEndedPending ? ActiveAttack.Montage->GetPlayLength() : Instance->GetPosition();
	const FTransform CurrentMesh = Character->GetMesh()->GetComponentTransform();
	const FTransform CurrentWeapon = GetWeaponTransform();
	TraceAttack(PreviousAttackPosition, Position, CurrentMesh, CurrentWeapon);
	if (ActionId != ActiveActionId) { return; } // Damage callbacks may interrupt or kill the attacker.
	SampleAttackWindow(Position);
	if (ActionId != ActiveActionId) { return; }
	PreviousMeshTransform = CurrentMesh;
	PreviousWeaponTransform = CurrentWeapon;
	if (bAttackEndedPending) { FinishAction(ActionId, false); }
}

void UDBCombatComponent::TraceAttack(float Previous, float Current, const FTransform& CurrentMesh, const FTransform& CurrentWeapon)
{
	if (!ActiveAttack.OverlapsActiveWindow(Previous, Current)) { return; }
	const uint64 ActionId = ActiveActionId;
	const float From = FMath::Max(Previous, ActiveAttack.ActiveStart);
	const float To = FMath::Min(Current, ActiveAttack.ActiveEnd);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt((To - From) * 120.0f), 1, 32);
	FTransform Last = SampleWeaponTransform(From, Current, CurrentMesh, CurrentWeapon);
	for (int32 Step = 1; Step <= Steps && ActiveActionId == ActionId; ++Step)
	{
		const float Time = FMath::Lerp(From, To, static_cast<float>(Step) / Steps);
		const FTransform Next = SampleWeaponTransform(Time, Current, CurrentMesh, CurrentWeapon);
		SweepBlade(Last, Next, ActionId);
		Last = Next;
	}
}

void UDBCombatComponent::SweepBlade(const FTransform& From, const FTransform& To, uint64 ActionId)
{
	const double Length = FMath::Max(From.TransformVector(BladeTipLocal - BladeBaseLocal).Size(),
		To.TransformVector(BladeTipLocal - BladeBaseLocal).Size());
	const int32 Samples = FMath::Clamp(FMath::CeilToInt(Length / (BladeTraceRadius * 2.0f)), 1, 64);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBBlade), false, GetOwner());
	TArray<FHitResult> Hits;
	for (int32 Sample = 0; Sample <= Samples && ActiveActionId == ActionId; ++Sample)
	{
		const FVector Local = FMath::Lerp(BladeBaseLocal, BladeTipLocal, static_cast<float>(Sample) / Samples);
		Hits.Reset();
		GetWorld()->SweepMultiByChannel(Hits, From.TransformPosition(Local), To.TransformPosition(Local),
			FQuat::Identity, DBCollision::Melee, FCollisionShape::MakeSphere(BladeTraceRadius), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Target = Hit.GetActor();
			if (ActiveActionId != ActionId) { return; }
			if (!IsValid(Target) || HitActors.Contains(Target)) { continue; }
			const UDBHealthComponent* Health = Target->FindComponentByClass<UDBHealthComponent>();
			if (!Health || Health->IsDead()) { continue; }
			FCollisionQueryParams Sight = Params;
			Sight.AddIgnoredActor(Target);
			FHitResult Obstruction;
			if (GetWorld()->LineTraceSingleByChannel(Obstruction, GetOwner()->GetActorLocation(), Hit.ImpactPoint,
				DBCollision::Melee, Sight)) { continue; }
			// One contact attempt per actor/window, even when that target rejects it via i-frames.
			HitActors.Add(Target);
			UGameplayStatics::ApplyPointDamage(Target, ActiveAttack.Damage,
				(To.TransformPosition(Local) - From.TransformPosition(Local)).GetSafeNormal(), Hit,
				CastChecked<ACharacter>(GetOwner())->GetController(), GetOwner(), nullptr);
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
	if (ActiveActionId != 0 || BufferedAction.Id != 0)
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s reset active #%llu / pending #%llu"),
			*GetOwner()->GetName(), ActiveActionId, BufferedAction.Id);
	}
	FinishAction(ActiveActionId);
	// Reset also stops any residual blend-out after an interruption.
	StopEvadeMontage();
	BufferedAction = {};
	bEvadeHeld = false;
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
