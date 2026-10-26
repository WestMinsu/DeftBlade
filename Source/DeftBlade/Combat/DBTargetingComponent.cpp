#include "Combat/DBTargetingComponent.h"

#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"

UDBTargetingComponent::UDBTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AActor* UDBTargetingComponent::SelectSoftTarget(const FVector& Direction)
{
	const FVector Forward = Direction.GetSafeNormal2D();
	const AActor* Owner = GetOwner();
	const UDBHealthComponent* OwnerHealth = Owner->FindComponentByClass<UDBHealthComponent>();
	if (Forward.IsNearlyZero() || !FMath::IsFinite(SoftTargetRange) || SoftTargetRange <= 0
		|| (OwnerHealth && OwnerHealth->IsDead()))
	{
		SoftTarget.Reset();
		return nullptr;
	}
	const FVector Origin = Owner->GetActorLocation();
	const float RangeSquared = FMath::Square(SoftTargetRange);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(SoftTargetHalfAngle, 1.f, 89.f)));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DBSoftTarget), false, Owner);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(SoftTargetRange), Query);
	TSet<AActor*> Seen;
	AActor* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!IsValid(Candidate) || Candidate == Owner || Seen.Contains(Candidate)) { continue; }
		Seen.Add(Candidate);
		const UDBHealthComponent* Health = Candidate->FindComponentByClass<UDBHealthComponent>();
		if (!Health || Health->IsDead() || Candidate->IsHidden() || !Candidate->CanBeDamaged()) { continue; }
		if (const APawn* Pawn = Cast<APawn>(Candidate))
		{
			// Existing enemy controllers define hostility. Health-bearing non-pawns include practice targets.
			const auto* Team = Cast<IGenericTeamAgentInterface>(Pawn->GetController());
			if (!Team || Team->GetTeamAttitudeTowards(*Owner) != ETeamAttitude::Hostile) { continue; }
		}
		const FVector Offset = Candidate->GetActorLocation() - Origin;
		const double DistanceSquared = Offset.SizeSquared();
		if (DistanceSquared > RangeSquared || Offset.SizeSquared2D() < 1.0
			|| FMath::Abs(Offset.Z) > MaxHeightDifference) { continue; }
		const double Dot = FVector::DotProduct(Forward, Offset.GetSafeNormal2D());
		if (Dot < MinDot) { continue; }
		FCollisionQueryParams Sight = Query;
		Sight.AddIgnoredActor(Candidate);
		if (GetWorld()->LineTraceTestByChannel(Origin, Candidate->GetActorLocation(), DBCollision::Melee, Sight)) { continue; }
		// Direction has more weight than distance. A small retention bias prevents near-tied combo targets flickering.
		const double Score = .75 * (1.0 - Dot) / (1.0 - MinDot) + .25 * DistanceSquared / RangeSquared
			- (Candidate == SoftTarget.Get() ? .10 : 0.0);
		if (Score < BestScore - UE_DOUBLE_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(Score, BestScore, UE_DOUBLE_SMALL_NUMBER)
				&& Best && Candidate->GetUniqueID() < Best->GetUniqueID()))
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
	const FVector Forward = Direction.GetSafeNormal2D();
	if (Forward.IsNearlyZero()) { return; }
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
