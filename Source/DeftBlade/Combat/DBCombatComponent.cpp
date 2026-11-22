#include "Combat/DBCombatComponent.h"

#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Character/DBEnemyCharacter.h"
#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBTargetingComponent.h"
#include "Combat/DBTrainingDummy.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBCombat, Log, All);

namespace
{
	bool IsAttackInput(EDBActionInput Input)
	{
		return Input == EDBActionInput::LightAttack || Input == EDBActionInput::HeavyAttack
		       || Input == EDBActionInput::Launcher || Input == EDBActionInput::SwitchWeapon;
	}
	bool IsAirLight(EDBAttackId Id)
	{
		return Id == EDBAttackId::AirLight1 || Id == EDBAttackId::AirLight2 || Id == EDBAttackId::AirLight3;
	}
	FName HandBone(EDBHitTraceSource Source)
	{
		return Source == EDBHitTraceSource::LeftHand ? TEXT("hand_l") : TEXT("hand_r");
	}
	int32 InputPriority(EDBActionInput Input)
	{
		return Input == EDBActionInput::Evade                                       ? 3
		       : Input == EDBActionInput::Jump || Input == EDBActionInput::Launcher ? 2
		                                                                            : 1;
	}

	const FDBAttackDefinition* FindWeaponAttack(const UDBWeaponData* Weapon, EDBAttackId Id)
	{
		const FDBAttackDefinition* Found = nullptr;
		if (Weapon)
		{
			for (const FDBAttackDefinition& Definition : Weapon->Attacks)
			{
				if (Definition.Id == Id)
				{
					if (Found)
					{
						return nullptr;
					}
					Found = &Definition;
				}
			}
		}
		return Found;
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
	StopAttackProtectionFeedback();
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
	AttachWeapon(bWeaponDrawn);
	if (WeaponData->AnimationClass && Mesh->GetAnimClass() != WeaponData->AnimationClass)
	{
		Mesh->SetAnimInstanceClass(WeaponData->AnimationClass);
	}
}

bool UDBCombatComponent::CanHolsterWeapon() const
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	return WeaponData && WeaponData->Mesh && !WeaponData->HolsterSocket.IsNone()
	       && Character->GetMesh()->DoesSocketExist(WeaponData->HolsterSocket);
}

void UDBCombatComponent::AttachWeapon(bool bDrawn)
{
	if (!WeaponMesh || !WeaponData)
	{
		return;
	}
	bWeaponDrawn = bDrawn || !CanHolsterWeapon();
	WeaponMesh->AttachToComponent(CastChecked<ACharacter>(GetOwner())->GetMesh(),
	    FAttachmentTransformRules::SnapToTargetNotIncludingScale,
	    bWeaponDrawn ? WeaponData->AttachSocket : WeaponData->HolsterSocket);
	WeaponMesh->SetRelativeTransform(bWeaponDrawn ? WeaponData->RelativeTransform : WeaponData->HolsterTransform);
	// Never sweep the jump between the back and hand as a weapon strike.
	PreviousWeaponTransform = GetTraceTransform(EDBHitTraceSource::Weapon);
}

bool UDBCombatComponent::PlayWeaponPresentation(bool bDrawn, float MaxDuration)
{
	StopWeaponPresentation();
	UAnimMontage* Montage = bDrawn ? WeaponData->DrawMontage : WeaponData->HolsterMontage;
	UAnimInstance* Anim = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance();
	if (!Montage || !Anim || Montage->HasRootMotion())
	{
		return false;
	}
	const float Rate = MaxDuration > 0.0f ? FMath::Max(1.0f, Montage->GetPlayLength() / MaxDuration) : 1.0f;
	if (Anim->Montage_Play(Montage, Rate, EMontagePlayReturnType::MontageLength, 0.0f, false) <= 0.0f)
	{
		return false;
	}
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	check(Instance);
	bDrawingWeapon = bDrawn;
	WeaponPresentationInstanceId = Instance->GetInstanceID();
	Instance->OnMontageEnded.BindUObject(this, &ThisClass::WeaponPresentationEnded, WeaponPresentationInstanceId);
	return true;
}

void UDBCombatComponent::StopWeaponPresentation()
{
	UAnimInstance* Anim = CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(WeaponPresentationInstanceId) : nullptr;
	WeaponPresentationInstanceId = INDEX_NONE;
	if (Instance)
	{
		Instance->OnMontageEnded.Unbind();
		Instance->Stop(FAlphaBlend(0.06f), true);
	}
}

bool UDBCombatComponent::CommitWeaponAttachment(int32 MontageInstanceId, bool bDrawn)
{
	if (MontageInstanceId == WeaponPresentationInstanceId && bDrawn == bDrawingWeapon && CanProcessInput())
	{
		const bool bWasDrawn = bWeaponDrawn;
		AttachWeapon(bDrawn);
		return bWasDrawn != bWeaponDrawn;
	}
	return false;
}

void UDBCombatComponent::WeaponPresentationEnded(UAnimMontage*, bool bInterrupted, int32 InstanceId)
{
	if (InstanceId != WeaponPresentationInstanceId)
	{
		return;
	}
	WeaponPresentationInstanceId = INDEX_NONE;
	if (!bInterrupted && CanProcessInput())
	{
		AttachWeapon(bDrawingWeapon);
	}
}

void UDBCombatComponent::UpdateWeaponPresentation()
{
	if (!CanHolsterWeapon())
	{
		return;
	}
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const bool bGrounded = Character->GetCharacterMovement()->IsMovingOnGround();
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!bGrounded && WeaponPresentationInstanceId != INDEX_NONE && !bDrawingWeapon)
	{
		StopWeaponPresentation();
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (ActionState != EDBActionState::None || BufferedAction.Id != 0)
	{
		HolsterAfter = Now + AutoHolsterDelay;
	}
	else if (bWeaponDrawn && bGrounded && Anim && WeaponPresentationInstanceId == INDEX_NONE
	         && !Anim->IsAnyMontagePlaying() && Now >= HolsterAfter)
	{
		if (!PlayWeaponPresentation(false))
		{
			AttachWeapon(false);
		}
	}
}

bool UDBCombatComponent::TryEquipWeapon(UDBWeaponData* NewWeapon)
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!CanProcessInput() || !Character->GetCharacterMovement()->IsMovingOnGround() || !CanEquipWeapon(NewWeapon))
	{
		return false;
	}
	if (IsAttacking())
	{
		const FDBAttackDefinition* Next = FindWeaponAttack(NewWeapon, ActiveAttack.SwitchAttack);
		if (ActiveAttack.SwitchAttack == EDBAttackId::None || !Next || !Next->IsValid() || IsAirLight(Next->Id)
		    || Next->Id == EDBAttackId::Launcher || Next->Id == EDBAttackId::AirHeavy)
		{
			return false;
		}
		const uint64 PreviousRequest = NextRequestId;
		QueueInput(EDBActionInput::SwitchWeapon, FVector::ZeroVector, NewWeapon);
		return NextRequestId != PreviousRequest;
	}
	const bool bOtherMontage = Anim
	                           && Anim->MontageInstances.ContainsByPredicate(
	                               [this](const FAnimMontageInstance* Instance)
	                               {
		                               return Instance
		                                      && (!WeaponData
		                                          || (Instance->Montage != WeaponData->DrawMontage
		                                              && Instance->Montage != WeaponData->HolsterMontage));
	                               });
	if (ActionState != EDBActionState::None || BufferedAction.Id != 0 || EvadeMotionId != 0 || !Anim || bOtherMontage)
	{
		return false;
	}
	StopWeaponPresentation();
	WeaponData = NewWeapon;
	ParryWindowEndsAt = 0.0;
	InitializeWeapon(WeaponMesh);
	HolsterAfter = GetWorld()->GetTimeSeconds() + AutoHolsterDelay;
	return true;
}

bool UDBCombatComponent::CanEquipWeapon(const UDBWeaponData* NewWeapon) const
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	if (!WeaponMesh || !NewWeapon || NewWeapon == WeaponData || !NewWeapon->Mesh || !NewWeapon->IdleAnimation
	    || !NewWeapon->LocomotionBlendSpace || NewWeapon->AnimationClass != Character->GetMesh()->GetAnimClass()
	    || !FMath::IsFinite(NewWeapon->BladeTraceRadius) || NewWeapon->BladeTraceRadius < 0.5f
	    || !Character->GetMesh()->DoesSocketExist(NewWeapon->AttachSocket))
	{
		return false;
	}
	const UStaticMeshSocket* Base = NewWeapon->Mesh->FindSocket(TEXT("BladeBase"));
	const UStaticMeshSocket* Tip = NewWeapon->Mesh->FindSocket(TEXT("BladeTip"));
	if (!Base || !Tip || FVector::DistSquared(Base->RelativeLocation, Tip->RelativeLocation) <= 1.0)
	{
		return false;
	}
	return true;
}

void UDBCombatComponent::BeginPlay()
{
	Super::BeginPlay();
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

void UDBCombatComponent::RequestAttack(bool bHeavy, const FVector& Direction)
{
	QueueInput(bHeavy ? EDBActionInput::HeavyAttack : EDBActionInput::LightAttack, Direction);
}

void UDBCombatComponent::RequestJump()
{
	QueueInput(EDBActionInput::Jump);
}

void UDBCombatComponent::RequestAttack(EDBAttackId Attack)
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const FDBAttackDefinition* Definition = FindAttack(Attack);
	if (!Character->GetController() || Character->IsPlayerControlled() || !CanProcessInput()
	    || ActionState != EDBActionState::None || BufferedAction.Id != 0 || !Definition || !Definition->IsValid()
	    || !IsAttackMovementValid(Attack))
	{
		return;
	}
	const auto Input = Attack >= EDBAttackId::Light1 && Attack <= EDBAttackId::Light4 ? EDBActionInput::LightAttack
	                                                                                  : EDBActionInput::HeavyAttack;
	BufferedAction = {++NextRequestId, GetWorld()->GetTimeSeconds(), Input, nullptr, Attack};
}

void UDBCombatComponent::QueueInput(EDBActionInput Input, const FVector& Direction, UDBWeaponData* NewWeapon)
{
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bOpenParry = Input == EDBActionInput::LightAttack
	                        && (LastLightInputAt < 0.0 || Now - LastLightInputAt >= ParryInputInterval);
	if (Input == EDBActionInput::LightAttack)
	{
		LastLightInputAt = Now;
	}
	else
	{
		ParryWindowEndsAt = 0.0;
	}
	if (ActionState == EDBActionState::HitReaction)
	{
		return;
	}
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
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
	if (BufferedAction.Id != 0
	    && (ActionState != EDBActionState::None || !BufferedAction.IsExpired(Now, InputBufferLifetime))
	    && InputPriority(BufferedAction.Input) > InputPriority(Input))
	{
		return;
	}
	if (bOpenParry)
	{
		// Parry eligibility is independent of whether the combo has another attack node.
		OpenParryWindow(Direction);
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
	BufferedAction = {++NextRequestId, Now, Input, NewWeapon};
}

EDBAttackId UDBCombatComponent::NextAttack(EDBActionInput Input) const
{
	if (BufferedAction.Attack != EDBAttackId::None && Input == BufferedAction.Input)
	{
		return BufferedAction.Attack;
	}
	if (!IsAttackInput(Input))
	{
		return EDBAttackId::None;
	}
	if (Input == EDBActionInput::Launcher)
	{
		return EDBAttackId::Launcher;
	}
	if (Input == EDBActionInput::SwitchWeapon)
	{
		return IsAttacking() ? ActiveAttack.SwitchAttack : EDBAttackId::None;
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
	if (Id == EDBAttackId::BossLeap)
	{
		return Movement->IsMovingOnGround() || Movement->IsFlying();
	}
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
	return FindWeaponAttack(WeaponData, Id);
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
	StopWeaponPresentation();
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
	// Player menus clear player actions explicitly; paused enemies retain their committed attacks.
	if (GetWorld()->IsPaused() && !CastChecked<ACharacter>(GetOwner())->IsPlayerControlled())
	{
		return;
	}
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}
	RestoreSlamCollision();
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
	UpdateWeaponPresentation();
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
	ActiveAttackChainId = 0;
	ActionStartedAt = 0.0;
	MovementEndsAt = 0.0;
	ChainStartsAt = 0.0;
	HeldSlideTime = 0.0;
	bBlendingToSprint = false;
	RestoreSlamCollision();
	return true;
}

void UDBCombatComponent::HandleLanding(const FHitResult& Hit)
{
	ParryWindowEndsAt = 0.0;
	StopAirMovement();
	ResetAirEvade();
	bAirLightComboUsed = false;
	GroundJumpPressedAt = -1.0;
	if (GetAttackId() == EDBAttackId::AirHeavy && !bAttackLanded)
	{
		const uint64 ActionId = ActiveActionId;
		// CMC can land before this frame's mesh callback. Flush the last swept descent first.
		TraceSlamDescent();
		if (ActiveActionId != ActionId)
		{
			return;
		}
		bAttackLanded = true;
		bAttackMovementStarted = true;
		ApplySlamImpact(Hit);
		if (ActiveActionId != ActionId)
		{
			return;
		}
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

void UDBCombatComponent::StartAirMovement(float Duration, float UpwardSpeed)
{
	StopAirMovement();
	TSharedPtr<FRootMotionSource_ConstantForce> Motion = MakeShared<FRootMotionSource_ConstantForce>();
	Motion->InstanceName = TEXT("DBAirCombat");
	Motion->Priority = 90;
	Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
	Motion->Duration = Duration;
	Motion->Force = FVector(0, 0, UpwardSpeed);
	Motion->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
	UCharacterMovementComponent* Movement = CastChecked<ACharacter>(GetOwner())->GetCharacterMovement();
	// PhysFalling integrates the old and new velocities; both must start with the same ascent.
	Movement->Velocity.Z = UpwardSpeed;
	AirMotionId = Movement->ApplyRootMotionSource(Motion);
}

void UDBCombatComponent::ApplySlamImpact(const FHitResult& LandingHit)
{
	if (!WeaponData || !FMath::IsFinite(WeaponData->SlamRadius) || WeaponData->SlamRadius <= 0.0f)
	{
		return;
	}
	const uint64 ActionId = ActiveActionId;
	const FVector Origin = LandingHit.ImpactPoint + LandingHit.ImpactNormal * 4.0f;
	if (WeaponData->SlamSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, WeaponData->SlamSound, Origin);
	}
	if (WeaponData->SlamEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), WeaponData->SlamEffect, Origin,
		    FRotationMatrix::MakeFromZ(LandingHit.ImpactNormal).Rotator(), FVector(WeaponData->SlamRadius / 100.0f));
	}
	const FVector QueryOrigin = Origin + FVector(0, 0, 60);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBSlam), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, QueryOrigin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
	    FCollisionShape::MakeSphere(WeaponData->SlamRadius), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (ActiveActionId != ActionId)
		{
			return;
		}
		AActor* Target = Overlap.GetActor();
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!IsValid(Target) || !IsValid(Component))
		{
			continue;
		}
		// A ground shockwave must not reach another floor or a high airborne target.
		const ACharacter* Victim = Cast<ACharacter>(Target);
		const FVector Feet =
		    Target->GetActorLocation()
		    - FVector(0, 0,
		        Victim ? Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : Component->Bounds.BoxExtent.Z);
		if (FMath::Abs(Feet.Z - LandingHit.ImpactPoint.Z) > 120.0f)
		{
			continue;
		}
		FVector Contact = Target->GetActorLocation();
		Component->GetClosestPointOnCollision(QueryOrigin, Contact);
		const FVector Direction = (Contact - QueryOrigin).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FHitResult Hit(Target, Component, Contact, -Direction);
		ApplyAttackHit(Hit, Direction, QueryOrigin, ActionId);
	}
}

void UDBCombatComponent::BeginSlamCollision()
{
	UCapsuleComponent* Capsule = CastChecked<ACharacter>(GetOwner())->GetCapsuleComponent();
	if (!bSlamIgnoresPawns && Capsule->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
	{
		// Keep terrain blocking. Another character must not become the slam's landing surface.
		bSlamIgnoresPawns = true;
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
}

void UDBCombatComponent::RestoreSlamCollision(bool bForce)
{
	if (!bSlamIgnoresPawns || (!bForce && GetAttackId() == EDBAttackId::AirHeavy))
	{
		return;
	}
	UCapsuleComponent* Capsule = CastChecked<ACharacter>(GetOwner())->GetCapsuleComponent();
	if (!bForce)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DBSlamSeparation), false, GetOwner());
		GetWorld()->OverlapMultiByObjectType(Overlaps, Capsule->GetComponentLocation(), Capsule->GetComponentQuat(),
		    FCollisionObjectQueryParams(ECC_Pawn), Capsule->GetCollisionShape(-1.0f), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const UPrimitiveComponent* Other = Overlap.GetComponent();
			if (IsValid(Other) && Other->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) == ECR_Block)
			{
				// Re-enabling blocking inside a capsule would cause a random CMC jump-off/depenetration.
				return;
			}
		}
	}
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	bSlamIgnoresPawns = false;
}

void UDBCombatComponent::TraceSlamDescent()
{
	if (GetAttackId() != EDBAttackId::AirHeavy || !bAttackMovementStarted || bAttackLanded)
	{
		return;
	}
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Offset(0, 0, Capsule->GetScaledCapsuleHalfHeight() - Capsule->GetScaledCapsuleRadius());
	const FVector From = PreviousSlamLocation - Offset;
	const FVector To = Character->GetActorLocation() - Offset;
	PreviousSlamLocation = Character->GetActorLocation();
	const uint64 ActionId = ActiveActionId;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBSlamDescent), false, GetOwner());
	GetWorld()->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
	    FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius()), Params);
	for (const FHitResult& Hit : Hits)
	{
		if (ActiveActionId != ActionId)
		{
			return;
		}
		// Share the existing hit set with blade contacts and the landing blast: one damage application.
		ApplyAttackHit(Hit, -FVector::UpVector, From, ActionId);
	}
}

void UDBCombatComponent::HoldAirborne(ACharacter* Attacker)
{
	StartAirMovement(0.75f, FMath::Max(Attacker->GetVelocity().Z, 0.0));
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

void UDBCombatComponent::StartLauncherMovement()
{
	if (bAttackMovementStarted)
	{
		return;
	}
	bAttackMovementStarted = true;
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	if (Character->JumpCurrentCount == 0)
	{
		Character->Jump();
	}
	Character->LaunchCharacter(FVector(0, 0, LauncherUpwardSpeed), false, true);
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
				const bool bFollowingRise =
				    IsAirLight(AttackerCombat->GetAttackId()) || JuggleAttacker->GetVelocity().Z > 0.0;
				if (bFollowingRise)
				{
					JuggleHeight = JuggleAttacker->GetActorLocation().Z;
				}
				const double Error = JuggleHeight - Character->GetActorLocation().Z;
				const double Speed = Error * (1.0 - FMath::Exp(-8.0 * DeltaTime)) / DeltaTime;
				// Predict the next ascent speed, avoiding a one-frame overshoot when the attacker reaches its apex.
				const double RiseSpeed =
				    bFollowingRise
				        ? FMath::Max(0.0, JuggleAttacker->GetVelocity().Z
				                              + JuggleAttacker->GetCharacterMovement()->GetGravityZ() * DeltaTime)
				        : 0.0;
				AirMotion->Force = FVector(0, 0, RiseSpeed + FMath::Clamp(Speed, -160.0, 160.0));
			}
		}
		else if (IsAirLight(GetAttackId()) || GetAttackId() == EDBAttackId::AirHeavy)
		{
			const float Push = IsAirLight(GetAttackId())
			                       ? FMath::SmoothStep(0.0f, 0.05f, PreviousAttackPosition)
			                             * (1.0f - FMath::SmoothStep(0.13f, 0.24f, PreviousAttackPosition))
			                       : 0.0f;
			// Preserve the launch arc across links, then hold at its apex. A ceiling ends the rise.
			const double RiseSpeed =
			    Character->GetVelocity().Z > 0.0
			        ? FMath::Max(0.0, AirMotion->Force.Z + Character->GetCharacterMovement()->GetGravityZ() * DeltaTime)
			        : 0.0;
			AirMotion->Force = Character->GetActorForwardVector() * (180.0f * Push) + FVector(0, 0, RiseSpeed);
		}
		else
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
		if (!bAttackMovementStarted && PreviousAttackPosition >= ActiveAttack.HitWindows.Last().End)
		{
			// Whiffs still rise, after the grounded swing has had its chance to connect.
			StartLauncherMovement();
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
		PreviousSlamLocation = Character->GetActorLocation();
		Character->LaunchCharacter(FVector(0, 0, -SlamDownwardSpeed), false, true);
	}
}

void UDBCombatComponent::TryStartAttack(bool bTutorialParry)
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const bool bSwitching = BufferedAction.Input == EDBActionInput::SwitchWeapon;
	UDBWeaponData* NextWeapon = bSwitching ? BufferedAction.Weapon.Get() : WeaponData.Get();
	const FDBAttackDefinition* Definition = FindWeaponAttack(NextWeapon, NextAttack(BufferedAction.Input));
	const bool bContinuing = ActionState != EDBActionState::None;
	const uint64 ChainId = IsAttacking() ? ActiveAttackChainId : 0;
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Anim || !Definition || !Definition->IsValid() || !IsAttackMovementValid(Definition->Id)
	    || (bSwitching && (!CanEquipWeapon(NextWeapon) || !Character->GetCharacterMovement()->IsMovingOnGround()))
	    || !ConfigureTrace(*Definition, *NextWeapon))
	{
		BufferedAction = {};
		return;
	}
	const float RiseSpeed = (IsAirLight(Definition->Id) || Definition->Id == EDBAttackId::AirHeavy)
	                                && (GetAttackId() == EDBAttackId::Launcher || IsAirLight(GetAttackId()))
	                            ? FMath::Max(Character->GetVelocity().Z, 0.0)
	                            : 0.0;
	if (bContinuing)
	{
		StopEvadeMovement(false);
		FinishAction(ActiveActionId, false);
	}
	float StartPosition = 0.0f;
	float Duration;
	if (bTutorialParry)
	{
		// The incoming weapon is already at contact. Start at the light swing's short final windup.
		const float Rate = Definition->PlayRate * Definition->Montage->RateScale;
		StartPosition = FMath::Max(0.0f, Definition->HitWindows[0].Start - 0.045f * Rate);
		Duration = Anim->Montage_PlayWithBlendIn(Definition->Montage, FAlphaBlendArgs(0.035f), Definition->PlayRate,
		    EMontagePlayReturnType::MontageLength, StartPosition);
	}
	else
	{
		Duration = Anim->Montage_Play(Definition->Montage, Definition->PlayRate);
	}
	if (Duration <= 0.0f)
	{
		BufferedAction = {};
		return;
	}
	if (bSwitching)
	{
		WeaponData = NextWeapon;
		ParryWindowEndsAt = 0.0;
		InitializeWeapon(WeaponMesh);
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
	PreviousAttackPosition = StartPosition;
	HitActors.Reset();
	bAttackEndedPending = false;
	bAttackMovementStarted = false;
	bAttackLanded = false;
	ActionStartedAt = GetWorld()->GetTimeSeconds();
	ActiveActionId = BufferedAction.Id;
	const bool bLight = bSwitching ? ActiveAttack.Id >= EDBAttackId::Light1 && ActiveAttack.Id <= EDBAttackId::Light4
	                               : BufferedAction.Input == EDBActionInput::LightAttack;
	ActionState = bLight ? EDBActionState::LightAttack : EDBActionState::HeavyAttack;
	ActiveAttackChainId = ChainId != 0 ? ChainId : ActiveActionId;
	BufferedAction = {};
	if (ActiveAttack.Id == EDBAttackId::AirHeavy)
	{
		BeginSlamCollision();
	}
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
	UpdateAttackProtectionFeedback();
	OnAttackStarted.Broadcast();
	HolsterAfter = GetWorld()->GetTimeSeconds() + AutoHolsterDelay;
	if (!bWeaponDrawn)
	{
		const float DrawTime = ActiveAttack.HitWindows[0].Start / ActiveAttack.PlayRate - 0.03f;
		if (!Character->GetCharacterMovement()->IsMovingOnGround() || DrawTime < 0.06f
		    || !PlayWeaponPresentation(true, DrawTime))
		{
			AttachWeapon(true);
		}
	}
	if (IsAirLight(ActiveAttack.Id) || ActiveAttack.Id == EDBAttackId::AirHeavy)
	{
		StartAirMovement(ActiveAttack.Montage->GetPlayLength() / ActiveAttack.PlayRate, RiseSpeed);
	}
	PreviousMeshTransform = Character->GetMesh()->GetComponentTransform();
	PreviousWeaponTransform = GetTraceTransform(EDBHitTraceSource::Weapon);
	PreviousLeftHandTransform = GetTraceTransform(EDBHitTraceSource::LeftHand);
	PreviousRightHandTransform = GetTraceTransform(EDBHitTraceSource::RightHand);
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
	StopWeaponPresentation();
	StopAttackProtectionFeedback();
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

bool UDBCombatComponent::ConfigureTrace(const FDBAttackDefinition& Definition, const UDBWeaponData& Data)
{
	if (!FMath::IsFinite(Data.BladeTraceRadius) || Data.BladeTraceRadius < 0.5f)
	{
		return false;
	}
	const USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	const bool bWeapon = Definition.HitWindows.ContainsByPredicate(
	    [](const FDBAttackWindow& Window)
	    {
		    return Window.TraceSource == EDBHitTraceSource::Weapon;
	    });
	for (const FDBAttackWindow& Window : Definition.HitWindows)
	{
		if (Window.TraceSource != EDBHitTraceSource::Weapon
		    && (Window.TraceSource > EDBHitTraceSource::RightHand
		        || Mesh->GetBoneIndex(HandBone(Window.TraceSource)) == INDEX_NONE))
		{
			return false;
		}
	}
	if (bWeapon)
	{
		const UStaticMeshSocket* Base = Data.Mesh ? Data.Mesh->FindSocket(TEXT("BladeBase")) : nullptr;
		const UStaticMeshSocket* Tip = Data.Mesh ? Data.Mesh->FindSocket(TEXT("BladeTip")) : nullptr;
		if (!WeaponMesh || (&Data == WeaponData.Get() && WeaponMesh->GetStaticMesh() != Data.Mesh) || !Base || !Tip
		    || FVector::DistSquared(Base->RelativeLocation, Tip->RelativeLocation) <= 1.0)
		{
			UE_LOG(LogDBCombat, Warning, TEXT("Assign BladeBase/BladeTip sockets on %s's weapon."),
			    *GetOwner()->GetName());
			return false;
		}
		BladeBaseLocal = Base->RelativeLocation;
		BladeTipLocal = Tip->RelativeLocation;
	}
	return true;
}

FTransform UDBCombatComponent::GetTraceTransform(EDBHitTraceSource Source) const
{
	const USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	if (Source != EDBHitTraceSource::Weapon)
	{
		return Mesh->GetSocketTransform(HandBone(Source));
	}
	// Attached components may update later than the bone-finalization callback.
	return WeaponMesh ? WeaponMesh->GetRelativeTransform() * Mesh->GetSocketTransform(WeaponMesh->GetAttachSocketName())
	                  : FTransform::Identity;
}

const FTransform& UDBCombatComponent::GetPreviousTraceTransform(EDBHitTraceSource Source) const
{
	return Source == EDBHitTraceSource::Weapon     ? PreviousWeaponTransform
	       : Source == EDBHitTraceSource::LeftHand ? PreviousLeftHandTransform
	                                               : PreviousRightHandTransform;
}

FTransform UDBCombatComponent::SampleTraceTransform(float Position, float CurrentPosition,
    const FTransform& CurrentMesh, const FTransform& CurrentTrace, EDBHitTraceSource Source) const
{
	if (FMath::IsNearlyEqual(Position, PreviousAttackPosition))
	{
		return GetPreviousTraceTransform(Source);
	}
	if (FMath::IsNearlyEqual(Position, CurrentPosition) && !bAttackEndedPending)
	{
		return CurrentTrace;
	}

	// A hitch may cross the entire hit window. Recover the authored poses inside
	// that interval instead of tracing the later recovery pose or a single chord.
	USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
	const FBoneContainer& Bones = Mesh->GetAnimInstance()->GetRequiredBones();
	const FName SocketName = Source == EDBHitTraceSource::Weapon ? WeaponMesh->GetAttachSocketName() : HandBone(Source);
	const USkeletalMeshSocket* Socket = Mesh->GetSocketByName(SocketName);
	const FCompactPoseBoneIndex Bone =
	    Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh->GetBoneIndex(Socket ? Socket->BoneName : SocketName)));
	const FAnimTrack* Track = ActiveAttack.Montage->GetAnimationData(TEXT("DefaultSlot"));
	if (!Track || Bone == INDEX_NONE)
	{
		return CurrentTrace;
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
	const FTransform Offset =
	    Source == EDBHitTraceSource::Weapon ? WeaponMesh->GetRelativeTransform() : FTransform::Identity;
	return Offset * SocketLocal * ComponentPose.GetComponentSpaceTransform(Bone) * MeshAtTime;
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
	if (!bWeaponDrawn && Position >= ActiveAttack.HitWindows[0].Start)
	{
		// A missing/late notify or a long frame cannot leave a live attack tracing from the back.
		StopWeaponPresentation();
		AttachWeapon(true);
	}
	if (ActiveAttack.Id == EDBAttackId::AirHeavy && !bAttackLanded && Position >= 0.32f)
	{
		// Hold the falling pose; recovery resumes from HandleLanding, never by forcing Walking.
		Position = 0.32f;
		Anim->Montage_SetPosition(ActiveAttack.Montage, Position);
		Anim->Montage_Pause(ActiveAttack.Montage);
	}
	UpdateAttackProtectionFeedback();
	const FTransform CurrentMesh = Character->GetMesh()->GetComponentTransform();
	const FTransform CurrentWeapon = GetTraceTransform(EDBHitTraceSource::Weapon);
	TraceAttack(PreviousAttackPosition, Position, CurrentMesh, CurrentWeapon);
	if (GetWorld()->IsPaused())
	{
		return;
	}
	if (ActionId == ActiveActionId)
	{
		TraceSlamDescent();
	}
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
	PreviousWeaponTransform = CurrentWeapon;
	PreviousLeftHandTransform = GetTraceTransform(EDBHitTraceSource::LeftHand);
	PreviousRightHandTransform = GetTraceTransform(EDBHitTraceSource::RightHand);
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
	if (ActiveAttack.Id == EDBAttackId::AirHeavy && bAttackLanded)
	{
		// Landing owns the remaining damage; recovery must not start another blade contact.
		return;
	}
	const uint64 ActionId = ActiveActionId;
	if (ActiveAttack.Id == EDBAttackId::AirHeavy && FMath::IsNearlyEqual(Previous, Current) && IsAttackWindowOpen()
	    && ActiveAttack.HitWindows.IsValidIndex(ActiveHitWindow))
	{
		// Montage time is held, but CMC is still moving the blade through the world.
		const EDBHitTraceSource Source = ActiveAttack.HitWindows[ActiveHitWindow].TraceSource;
		SweepAttack(GetPreviousTraceTransform(Source), GetTraceTransform(Source), ActionId, Source);
		return;
	}
	for (int32 Index = 0; Index < ActiveAttack.HitWindows.Num() && ActiveActionId == ActionId; ++Index)
	{
		const FDBAttackWindow Window = ActiveAttack.HitWindows[Index];
		if (!Window.Overlaps(Previous, Current))
		{
			continue;
		}
		if (ActiveHitWindow != Index)
		{
			HitActors.Reset();
			ActiveHitWindow = Index;
		}
		const float From = FMath::Max(Previous, Window.Start);
		const float To = FMath::Min(Current, Window.End);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt((To - From) * 120.0f), 1, 32);
		const FTransform CurrentTrace =
		    Window.TraceSource == EDBHitTraceSource::Weapon ? CurrentWeapon : GetTraceTransform(Window.TraceSource);
		FTransform Last = SampleTraceTransform(From, Current, CurrentMesh, CurrentTrace, Window.TraceSource);
		for (int32 Step = 1; Step <= Steps && ActiveActionId == ActionId; ++Step)
		{
			const float Time = FMath::Lerp(From, To, static_cast<float>(Step) / Steps);
			const FTransform Next = SampleTraceTransform(Time, Current, CurrentMesh, CurrentTrace, Window.TraceSource);
			SweepAttack(Last, Next, ActionId, Window.TraceSource);
			Last = Next;
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

bool UDBCombatComponent::CanParry() const
{
	if (ActionState != EDBActionState::None && ActionState != EDBActionState::LightAttack)
	{
		return false;
	}
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const FDBAttackDefinition* Definition =
	    ActionState == EDBActionState::LightAttack ? &ActiveAttack : FindAttack(EDBAttackId::Light1);
	return Definition && Definition->bCanParry && Definition->IsValid() && CanProcessInput()
	       && GetOwner()->CanBeDamaged() && Character->GetCharacterMovement()->IsMovingOnGround()
	       && Character->GetMesh()->GetAnimInstance() && WeaponMesh && WeaponMesh->GetStaticMesh();
}

void UDBCombatComponent::OpenParryWindow(const FVector& Direction)
{
	if (!CanParry())
	{
		return;
	}
	const FVector Intent = Direction.IsNearlyZero() ? GetOwner()->GetActorForwardVector() : Direction;
	if (UDBTargetingComponent* Targeting = GetOwner()->FindComponentByClass<UDBTargetingComponent>())
	{
		ParryDirection = Targeting->GetAttackDirection(Intent);
	}
	else
	{
		ParryDirection = Intent.GetSafeNormal2D();
	}
	ParryWindowEndsAt = GetWorld()->GetTimeSeconds() + ParryDuration;
}

bool UDBCombatComponent::TryParry(UDBCombatComponent& Attacker, const FVector& ImpactPoint)
{
	// Called only for a real, unobstructed hostile blade contact, immediately before applying damage.
	if (ParryWindowEndsAt == 0.0 || GetWorld()->GetTimeSeconds() >= ParryWindowEndsAt || !CanParry()
	    || FVector::DotProduct(ParryDirection,
	           (Attacker.GetOwner()->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal2D())
	           < 0.5f)
	{
		return false;
	}
	ResolveParry(Attacker, ImpactPoint, ParryDirection);
	return true;
}

bool UDBCombatComponent::ParryTutorialAttack(UDBCombatComponent& Attacker, uint64 ActionId, const FVector& ImpactPoint)
{
	const auto* Character = CastChecked<ACharacter>(GetOwner());
	const auto* Health = Character->FindComponentByClass<UDBHealthComponent>();
	if (!Character->IsPlayerControlled() || !Health || Health->IsDead() || GetWorld()->IsPaused()
	    || !IsValid(Attacker.GetOwner()) || ActionId == 0 || Attacker.ActiveActionId != ActionId
	    || !Attacker.HasPendingParryLesson())
	{
		return false;
	}
	const FVector Direction =
	    (Attacker.GetOwner()->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal2D();
	Attacker.OnTutorialParryContact.Unbind();
	ResolveParry(Attacker, ImpactPoint, Direction);
	// The tutorial confirmed this press in UI input mode. Start the authored swing in the same frame as the clash.
	const EDBAttackId Swing =
	    Character->GetCharacterMovement()->IsFalling() ? EDBAttackId::AirLight1 : EDBAttackId::Light1;
	BufferedAction = {++NextRequestId, GetWorld()->GetTimeSeconds(), EDBActionInput::LightAttack, nullptr, Swing};
	TryStartAttack(true);
	return true;
}

void UDBCombatComponent::ResolveParry(
    UDBCombatComponent& Attacker, const FVector& ImpactPoint, const FVector& Direction)
{
	ParryWindowEndsAt = 0.0;
	GetOwner()->SetActorRotation(Direction.Rotation());
	StopWeaponPresentation();
	AttachWeapon(true);
	HolsterAfter = GetWorld()->GetTimeSeconds() + AutoHolsterDelay;
	// The same light swing may damage the enemy just after the clash. Give the parry a clear beat.
	HitFeedbackSuppressedUntil = FPlatformTime::Seconds() + 0.14;
	if (LastHitSound.IsValid())
	{
		LastHitSound->Stop();
		LastHitSound.Reset();
	}
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
		if (ParryHitStopEndsAt == 0.0)
		{
			TimeDilationBeforeParry = UGameplayStatics::GetGlobalTimeDilation(this);
		}
		UGameplayStatics::SetGlobalTimeDilation(this, FMath::Min(TimeDilationBeforeParry, 0.05f));
		ParryHitStopEndsAt = FPlatformTime::Seconds() + ParryHitStopDuration;
	}
	const uint64 AttackerActionId = Attacker.ActiveActionId;
	Attacker.OnParried.Broadcast(GetOwner());
	if (IsValid(&Attacker) && Attacker.ActiveActionId == AttackerActionId)
	{
		Attacker.ResetCombat();
	}
	OnParrySucceeded.Broadcast();
}

void UDBCombatComponent::UpdateAttackProtectionFeedback()
{
	const bool bActive = WeaponData && WeaponMesh
	                     && (WeaponData->AttackProtectionMaterial || WeaponData->AttackProtectionSound)
	                     && HasAttackStaggerResistance();
	if (!bActive)
	{
		StopAttackProtectionFeedback();
		return;
	}
	if (bProtectionFeedbackActive)
	{
		return;
	}
	bProtectionFeedbackActive = true;
	PreviousWeaponOverlay = WeaponMesh->GetOverlayMaterial();
	if (WeaponData->AttackProtectionMaterial)
	{
		WeaponMesh->SetOverlayMaterial(WeaponData->AttackProtectionMaterial);
	}
	if (WeaponData->AttackProtectionSound)
	{
		ProtectionSound = UGameplayStatics::SpawnSoundAttached(WeaponData->AttackProtectionSound, WeaponMesh, NAME_None,
		    FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true);
	}
}

void UDBCombatComponent::StopAttackProtectionFeedback()
{
	if (!bProtectionFeedbackActive)
	{
		return;
	}
	bProtectionFeedbackActive = false;
	if (WeaponMesh)
	{
		WeaponMesh->SetOverlayMaterial(PreviousWeaponOverlay);
	}
	PreviousWeaponOverlay = nullptr;
	if (ProtectionSound.IsValid())
	{
		ProtectionSound->Stop();
	}
	ProtectionSound.Reset();
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

void UDBCombatComponent::SweepAttack(
    const FTransform& From, const FTransform& To, uint64 ActionId, EDBHitTraceSource Source)
{
	const FVector Base = Source == EDBHitTraceSource::Weapon ? BladeBaseLocal : FVector::ZeroVector;
	const FVector Tip = Source == EDBHitTraceSource::Weapon ? BladeTipLocal : FVector::ZeroVector;
	const double Length = FMath::Max(From.TransformVector(Tip - Base).Size(), To.TransformVector(Tip - Base).Size());
	const int32 Samples = Source == EDBHitTraceSource::Weapon
	                          ? FMath::Clamp(FMath::CeilToInt(Length / (WeaponData->BladeTraceRadius * 2.0f)), 1, 64)
	                          : 0;
	const float Radius = Source == EDBHitTraceSource::Weapon && ActiveAttack.WeaponTraceRadius > 0.0f
	                         ? ActiveAttack.WeaponTraceRadius
	                         : WeaponData->BladeTraceRadius;
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DBBlade), false, GetOwner());
	TArray<FHitResult> Hits;
	for (int32 Sample = 0; Sample <= Samples && ActiveActionId == ActionId; ++Sample)
	{
		const FVector Local = Samples ? FMath::Lerp(Base, Tip, static_cast<float>(Sample) / Samples) : Base;
		Hits.Reset();
		GetWorld()->SweepMultiByChannel(Hits, From.TransformPosition(Local), To.TransformPosition(Local),
		    FQuat::Identity, DBCollision::Melee, Shape, Params);
		for (const FHitResult& Hit : Hits)
		{
			ApplyAttackHit(Hit, (To.TransformPosition(Local) - From.TransformPosition(Local)).GetSafeNormal(),
			    GetOwner()->GetActorLocation(), ActionId);
			if (ActiveActionId != ActionId)
			{
				return;
			}
		}
	}
}

void UDBCombatComponent::ApplyAttackHit(
    const FHitResult& Hit, const FVector& Direction, const FVector& TraceOrigin, uint64 ActionId)
{
	AActor* Target = Hit.GetActor();
	if (ActiveActionId != ActionId || GetWorld()->IsPaused())
	{
		return;
	}
	if (!IsValid(Target) || HitActors.Contains(Target))
	{
		return;
	}
	const auto* Team = Cast<IGenericTeamAgentInterface>(CastChecked<ACharacter>(GetOwner())->GetController());
	if (Team && Team->GetTeamAttitudeTowards(*Target) != ETeamAttitude::Hostile)
	{
		return;
	}
	const UDBHealthComponent* Health = Target->FindComponentByClass<UDBHealthComponent>();
	if (!Health || Health->IsDead())
	{
		return;
	}
	FCollisionQueryParams Sight(SCENE_QUERY_STAT(DBAttackSight), false, GetOwner());
	Sight.AddIgnoredActor(Target);
	FHitResult Obstruction;
	if (GetWorld()->LineTraceSingleByChannel(Obstruction, TraceOrigin, Hit.ImpactPoint, DBCollision::Melee, Sight))
	{
		return;
	}
	// One contact attempt per actor/window, even when that target rejects it via i-frames.
	HitActors.Add(Target);
	UDBCombatComponent* VictimCombat = Target->FindComponentByClass<UDBCombatComponent>();
	if (OnTutorialParryContact.IsBound() && VictimCombat && Target->CanBeDamaged() && !VictimCombat->IsInvulnerable())
	{
		const auto* Pawn = Cast<APawn>(Target);
		if (Pawn && Pawn->IsPlayerControlled())
		{
			// Freeze a swing that actually reaches the player, before applying either damage or a parry.
			if (OnTutorialParryContact.Execute(this, Hit.ImpactPoint))
			{
				return;
			}
			// A missing tutorial UI must never leave a protected enemy that cannot be defeated.
			OnTutorialParryContact.Unbind();
		}
	}
	if (VictimCombat && VictimCombat->TryParry(*this, Hit.ImpactPoint))
	{
		return;
	}
	const auto* Enemy = Cast<ADBEnemyCharacter>(Target);
	const bool bResistsStagger =
	    Enemy ? Enemy->HasStaggerResistance() : VictimCombat && VictimCombat->HasAttackStaggerResistance();
	// Snapshot before damage callbacks: an accepted boss launcher consumes its one opening.
	const bool bCanLaunch = !Enemy || Enemy->CanBeLaunched();
	const EDBAttackId Attack = ActiveAttack.Id;
	const UDBWeaponData* AttackWeapon = WeaponData;
	const uint64 ChainId = ActiveAttackChainId;
	// Damage callbacks can finish/switch this action; retain the contact's authored feedback.
	UParticleSystem* HitEffect = ActiveAttack.HitEffect;
	USoundBase* HitSound = ActiveAttack.HitSound;
	float HitEffectScale = ActiveAttack.HitEffectScale;
	const UDBWeaponData* VictimWeapon = VictimCombat ? VictimCombat->GetWeaponData() : nullptr;
	UParticleSystem* ProtectedEffect =
	    bResistsStagger && VictimWeapon ? VictimWeapon->ProtectedHitEffect.Get() : nullptr;
	USoundBase* ProtectedSound = bResistsStagger && VictimWeapon ? VictimWeapon->ProtectedHitSound.Get() : nullptr;
	const float Applied = UGameplayStatics::ApplyPointDamage(Target, ActiveAttack.Damage, Direction, Hit,
	    CastChecked<ACharacter>(GetOwner())->GetController(), GetOwner(), nullptr);
	if (Applied > 0.0f && FPlatformTime::Seconds() >= HitFeedbackSuppressedUntil)
	{
		if (!Health->IsDead())
		{
			if (ProtectedEffect)
			{
				HitEffect = ProtectedEffect;
				HitEffectScale = 1.0f;
			}
			if (ProtectedSound)
			{
				HitSound = ProtectedSound;
			}
		}
		if (HitEffect)
		{
			UGameplayStatics::SpawnEmitterAtLocation(
			    GetWorld(), HitEffect, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVector(HitEffectScale));
		}
		if (HitSound)
		{
			LastHitSound = UGameplayStatics::SpawnSoundAtLocation(this, HitSound, Hit.ImpactPoint);
		}
	}
	const auto* Dummy = Cast<ADBTrainingDummy>(Target);
	if (Applied > 0.0f && (!Dummy || !Dummy->IsRepeatablePractice()))
	{
		OnAttackHit.Broadcast(AttackWeapon, Attack, ActionId, ChainId);
	}
	ACharacter* Victim = Cast<ACharacter>(Target);
	if (Applied > 0.0f && IsValid(Victim) && IsValid(Health) && !Health->IsDead() && ActiveActionId == ActionId
	    && !bResistsStagger)
	{
		if (Attack == EDBAttackId::Launcher)
		{
			if (bCanLaunch)
			{
				// Start both arcs at contact instead of lifting the blade away before it hits.
				StartLauncherMovement();
				Victim->LaunchCharacter(FVector(0, 0, LauncherUpwardSpeed), true, true);
			}
		}
		else if (Victim->GetCharacterMovement()->IsFalling())
		{
			if (Attack == EDBAttackId::AirHeavy)
			{
				Victim->LaunchCharacter(FVector(0, 0, -SlamDownwardSpeed), true, true);
			}
			else if (IsAirLight(Attack))
			{
				if (VictimCombat)
				{
					VictimCombat->HoldAirborne(CastChecked<ACharacter>(GetOwner()));
				}
			}
		}
		if (Attack != EDBAttackId::Launcher && ActiveAttack.GroundKnockbackDistance > 0.0f
		    && (Victim->GetCharacterMovement()->IsMovingOnGround() || Attack == EDBAttackId::AirHeavy))
		{
			float Distance = ActiveAttack.GroundKnockbackDistance;
			if (Attack == EDBAttackId::AirHeavy)
			{
				// Clear the landing space without throwing nearby combo targets out of reach.
				const ACharacter* Attacker = CastChecked<ACharacter>(GetOwner());
				const float Clearance = Attacker->GetCapsuleComponent()->GetScaledCapsuleRadius()
				                        + Victim->GetCapsuleComponent()->GetScaledCapsuleRadius() + 4.0f;
				Distance =
				    FMath::Clamp(Clearance - FVector::Dist2D(Attacker->GetActorLocation(), Victim->GetActorLocation()),
				        0.0f, Distance);
				if (Distance <= UE_SMALL_NUMBER)
				{
					return;
				}
			}
			const FVector Away = (Victim->GetActorLocation() - GetOwner()->GetActorLocation())
			                         .GetSafeNormal2D(UE_SMALL_NUMBER, GetOwner()->GetActorForwardVector());
			auto* Movement = Victim->GetCharacterMovement();
			TSharedPtr<FRootMotionSource_ConstantForce> Motion = MakeShared<FRootMotionSource_ConstantForce>();
			Motion->InstanceName = TEXT("DBGroundKnockback");
			Motion->Priority = 100;
			Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
			Motion->Duration = .18f;
			Motion->Force = Away * (Distance / Motion->Duration);
			Motion->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
			Motion->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
			// End horizontal motion without slowing a fall if collision carries the victim off a ledge.
			Motion->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
			Motion->FinishVelocityParams.ClampVelocity = 0.0f;
			Movement->RemoveRootMotionSource(Motion->InstanceName);
			Movement->ApplyRootMotionSource(Motion);
			// Keep both attack poses, while letting the victim's source create real separation.
			for (UDBCombatComponent* Combat : {this, VictimCombat})
			{
				if (!Combat)
				{
					continue;
				}
				if (UAnimInstance* Anim = CastChecked<ACharacter>(Combat->GetOwner())->GetMesh()->GetAnimInstance())
				{
					if (FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(Combat->AttackMontageInstanceId);
					    Instance && !Instance->IsRootMotionDisabled())
					{
						Instance->PushDisableRootMotion();
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
	StopWeaponPresentation();
	HolsterAfter = GetWorld()->GetTimeSeconds() + AutoHolsterDelay;
	ParryWindowEndsAt = 0.0;
	LastLightInputAt = -1.0;
	StopParryHitStop();
	if (ActiveActionId != 0 || BufferedAction.Id != 0)
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s reset active #%llu / pending #%llu"), *GetOwner()->GetName(),
		    ActiveActionId, BufferedAction.Id);
	}
	FinishAction(ActiveActionId);
	RestoreSlamCollision(true);
	StopAirMovement();
	UCharacterMovementComponent* Movement = CastChecked<ACharacter>(GetOwner())->GetCharacterMovement();
	if (TSharedPtr<FRootMotionSource> Knockback = Movement->GetRootMotionSource(TEXT("DBGroundKnockback")))
	{
		// An interrupting launcher owns its new velocity; only a natural expiration should clamp it.
		Knockback->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
		Movement->RemoveRootMotionSource(Knockback->InstanceName);
	}
	// Reset also stops any residual blend-out after an interruption.
	StopEvadeMontage();
	BufferedAction = {};
	bEvadeHeld = false;
	GroundJumpPressedAt = -1.0;
}

void UDBCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(PoseFinalizedHandle);
	}
	ResetCombat();
	Super::EndPlay(EndPlayReason);
}
