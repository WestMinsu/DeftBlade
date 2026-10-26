#include "Combat/DBTargetingComponent.h"

#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBCombatComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "UnrealClient.h"

UDBTargetingComponent::UDBTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UDBTargetingComponent::IsEligible(AActor* Candidate, float Range, float Height) const
{
	if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsHidden() || !Candidate->CanBeDamaged())
	{
		return false;
	}
	const UDBHealthComponent* Health = Candidate->FindComponentByClass<UDBHealthComponent>();
	if (!Health || Health->IsDead())
	{
		return false;
	}
	if (const APawn* Pawn = Cast<APawn>(Candidate))
	{
		const auto* Team = Cast<IGenericTeamAgentInterface>(Pawn->GetController());
		if (!Team || Team->GetTeamAttitudeTowards(*GetOwner()) != ETeamAttitude::Hostile)
		{
			return false;
		}
	}
	const FVector Offset = Candidate->GetActorLocation() - GetOwner()->GetActorLocation();
	return Offset.SizeSquared2D() >= 1.0 && Offset.SizeSquared() <= FMath::Square(Range)
	       && FMath::Abs(Offset.Z) <= Height;
}

bool UDBTargetingComponent::HasSight(AActor* Candidate, const FVector& Origin) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DBTargetSight), false, GetOwner());
	Query.AddIgnoredActor(Candidate);
	return !GetWorld()->LineTraceTestByChannel(
	    Origin, Candidate->GetActorLocation() + FVector(0, 0, 30), DBCollision::Melee, Query);
}

void UDBTargetingComponent::GatherCandidates(float Range, TArray<AActor*>& Candidates) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DBTargetCandidates), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, GetOwner()->GetActorLocation(), FQuat::Identity,
	    FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Range), Query);
	TSet<AActor*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (IsValid(Actor) && !Seen.Contains(Actor))
		{
			Seen.Add(Actor);
			Candidates.Add(Actor);
		}
	}
}

AActor* UDBTargetingComponent::SelectSoftTarget(const FVector& Direction)
{
	const FVector Forward = Direction.GetSafeNormal2D();
	const UDBHealthComponent* OwnerHealth = GetOwner()->FindComponentByClass<UDBHealthComponent>();
	if (Forward.IsNearlyZero() || !FMath::IsFinite(SoftTargetRange) || SoftTargetRange <= 0
	    || (OwnerHealth && OwnerHealth->IsDead()))
	{
		SoftTarget.Reset();
		return nullptr;
	}
	const FVector Origin = GetOwner()->GetActorLocation();
	const float RangeSquared = FMath::Square(SoftTargetRange);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(SoftTargetHalfAngle, 1.f, 89.f)));
	TArray<AActor*> Candidates;
	GatherCandidates(SoftTargetRange, Candidates);
	AActor* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	for (AActor* Candidate : Candidates)
	{
		if (!IsEligible(Candidate, SoftTargetRange, MaxHeightDifference))
		{
			continue;
		}
		const FVector Offset = Candidate->GetActorLocation() - Origin;
		const double DistanceSquared = Offset.SizeSquared();
		const double Dot = FVector::DotProduct(Forward, Offset.GetSafeNormal2D());
		if (Dot < MinDot || !HasSight(Candidate, Origin))
		{
			continue;
		}
		// Direction has more weight than distance. A small retention bias prevents near-tied combo targets flickering.
		const double Score = .75 * (1.0 - Dot) / (1.0 - MinDot) + .25 * DistanceSquared / RangeSquared
		                     - (Candidate == SoftTarget.Get() ? .10 : 0.0);
		if (Score < BestScore - UE_DOUBLE_SMALL_NUMBER
		    || (FMath::IsNearlyEqual(Score, BestScore, UE_DOUBLE_SMALL_NUMBER) && Best
		        && Candidate->GetUniqueID() < Best->GetUniqueID()))
		{
			Best = Candidate;
			BestScore = Score;
		}
	}
	SoftTarget = Best;
	return Best;
}

void UDBTargetingComponent::AssistAttackFacing(const FVector& Direction)
{
	if (bLockOnActive)
	{
		if (IsEligible(LockedTarget.Get(), FMath::Max(LockOnRange, LockOnReleaseRange), LockOnMaxHeight))
		{
			GetOwner()->SetActorRotation(
			    FRotator(0, (GetTargetPoint() - GetOwner()->GetActorLocation()).Rotation().Yaw, 0));
			return;
		}
		ClearLockOn();
	}
	const FVector Forward = Direction.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return;
	}
	const float IntentYaw = Forward.Rotation().Yaw;
	float Yaw = IntentYaw;
	if (const AActor* Target = SelectSoftTarget(Forward))
	{
		const float TargetYaw = (Target->GetActorLocation() - GetOwner()->GetActorLocation()).Rotation().Yaw;
		const float Limit = FMath::Clamp(MaxAttackYawCorrection, 0.f, 90.f);
		Yaw += FMath::Clamp(FMath::FindDeltaAngleDegrees(IntentYaw, TargetYaw), -Limit, Limit);
	}
	GetOwner()->SetActorRotation(FRotator(0, Yaw, 0));
}

bool UDBTargetingComponent::CanUseLockOn() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const UDBHealthComponent* Health = GetOwner()->FindComponentByClass<UDBHealthComponent>();
	if (!PC || !PC->IsLocalController() || PC->IsMoveInputIgnored() || GetWorld()->IsPaused()
	    || (Health && Health->IsDead()))
	{
		return false;
	}
	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	return Viewport && !Viewport->IgnoreInput() && (!Viewport->Viewport || Viewport->Viewport->HasFocus());
}

bool UDBTargetingComponent::ProjectCandidate(AActor* Candidate, FVector2D& Screen) const
{
	const auto* PC = Cast<APlayerController>(CastChecked<APawn>(GetOwner())->GetController());
	if (!PC || !PC->ProjectWorldLocationToScreen(Candidate->GetActorLocation() + FVector(0, 0, 30), Screen, true))
	{
		return false;
	}
	int32 Width, Height;
	PC->GetViewportSize(Width, Height);
	return Screen.X >= 0 && Screen.Y >= 0 && Screen.X <= Width && Screen.Y <= Height;
}

FVector UDBTargetingComponent::GetTargetPoint() const
{
	return LockedTarget.IsValid() ? LockedTarget->GetActorLocation() + FVector(0, 0, 30) : FVector::ZeroVector;
}

void UDBTargetingComponent::ToggleLockOn()
{
	if (bLockOnActive)
	{
		ClearLockOn();
		return;
	}
	if (!CanUseLockOn())
	{
		return;
	}
	const UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		return;
	}
	TArray<AActor*> Candidates;
	GatherCandidates(LockOnRange, Candidates);
	AActor* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	for (AActor* Candidate : Candidates)
	{
		FVector2D Screen;
		if (!IsEligible(Candidate, LockOnRange, LockOnMaxHeight) || !ProjectCandidate(Candidate, Screen)
		    || !HasSight(Candidate, GetOwner()->GetActorLocation())
		    || !HasSight(Candidate, Camera->GetComponentLocation()))
		{
			continue;
		}
		const FVector View = (Candidate->GetActorLocation() - Camera->GetComponentLocation()).GetSafeNormal();
		const double Score = .8 * (1.0 - FVector::DotProduct(Camera->GetForwardVector(), View))
		                     + .2 * FVector::DistSquared(GetOwner()->GetActorLocation(), Candidate->GetActorLocation())
		                           / FMath::Square(LockOnRange);
		if (Score < BestScore || (Score == BestScore && Best && Candidate->GetUniqueID() < Best->GetUniqueID()))
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	if (Best)
	{
		SetLockedTarget(Best);
	}
}

void UDBTargetingComponent::SetLockedTarget(AActor* Target)
{
	if (!bLockOnActive)
	{
		auto* Movement = CastChecked<ACharacter>(GetOwner())->GetCharacterMovement();
		bWasOrientingToMovement = Movement->bOrientRotationToMovement;
		Movement->bOrientRotationToMovement = false;
		bLockOnActive = true;
		bStickSwitchArmed = false;
		SetComponentTickEnabled(true);
	}
	LockedTarget = Target;
	OccludedTime = 0;
}

void UDBTargetingComponent::ClearLockOn()
{
	if (!bLockOnActive)
	{
		return;
	}
	bLockOnActive = false;
	LockedTarget.Reset();
	OccludedTime = 0;
	bStickSwitchArmed = false;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->bOrientRotationToMovement = bWasOrientingToMovement;
	}
	SetComponentTickEnabled(false);
}

void UDBTargetingComponent::SwitchTarget(float Direction)
{
	if (!IsLockedOn() || !CanUseLockOn() || FMath::IsNearlyZero(Direction))
	{
		return;
	}
	FVector2D CurrentScreen;
	if (!ProjectCandidate(LockedTarget.Get(), CurrentScreen))
	{
		return;
	}
	const UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		return;
	}
	TArray<AActor*> Candidates;
	GatherCandidates(LockOnRange, Candidates);
	AActor* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	for (AActor* Candidate : Candidates)
	{
		FVector2D Screen;
		if (Candidate == LockedTarget.Get() || !IsEligible(Candidate, LockOnRange, LockOnMaxHeight)
		    || !ProjectCandidate(Candidate, Screen) || (Screen.X - CurrentScreen.X) * FMath::Sign(Direction) <= 1.0
		    || !HasSight(Candidate, GetOwner()->GetActorLocation())
		    || !HasSight(Candidate, Camera->GetComponentLocation()))
		{
			continue;
		}
		const double Score = FMath::Abs(Screen.X - CurrentScreen.X) + .25 * FMath::Abs(Screen.Y - CurrentScreen.Y);
		if (Score < BestScore || (Score == BestScore && Best && Candidate->GetUniqueID() < Best->GetUniqueID()))
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	if (Best)
	{
		SetLockedTarget(Best);
	}
}

void UDBTargetingComponent::HandleStickSwitch(float Axis)
{
	if (FMath::Abs(Axis) < .25f)
	{
		bStickSwitchArmed = true;
	}
	else if (bStickSwitchArmed && FMath::Abs(Axis) >= .65f)
	{
		bStickSwitchArmed = false;
		SwitchTarget(Axis);
	}
}

void UDBTargetingComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!CanUseLockOn()
	    || !IsEligible(LockedTarget.Get(), FMath::Max(LockOnRange, LockOnReleaseRange), LockOnMaxHeight))
	{
		ClearLockOn();
		return;
	}
	const UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	const bool bVisible = HasSight(LockedTarget.Get(), GetOwner()->GetActorLocation()) && Camera
	                      && HasSight(LockedTarget.Get(), Camera->GetComponentLocation());
	OccludedTime = bVisible ? 0 : OccludedTime + DeltaTime;
	if (!bVisible && OccludedTime >= OcclusionGraceTime)
	{
		ClearLockOn();
		return;
	}
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	auto* PC = CastChecked<APlayerController>(Character->GetController());
	const FVector Focus = GetTargetPoint();
	// Aim from above the player so the follow camera frames both combatants without a position-feedback loop.
	const double ViewHeight = 110.0 + 0.28 * FVector::Dist2D(Focus, Character->GetActorLocation());
	FRotator View = (Focus - (Character->GetActorLocation() + FVector(0, 0, ViewHeight))).Rotation();
	View.Pitch = FMath::Clamp(View.Pitch, -45.0, 20.0);
	PC->SetControlRotation(FMath::RInterpTo(PC->GetControlRotation(), View, DeltaTime, 8.0f));
	const UDBCombatComponent* Combat = Character->FindComponentByClass<UDBCombatComponent>();
	if (!Combat || Combat->GetActionState() == EDBActionState::None)
	{
		const double Yaw = FMath::FixedTurn(Character->GetActorRotation().Yaw,
		    (Focus - Character->GetActorLocation()).Rotation().Yaw, 720.0 * DeltaTime);
		Character->SetActorRotation(FRotator(0, Yaw, 0));
	}
}

void UDBTargetingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearLockOn();
	Super::EndPlay(Reason);
}
