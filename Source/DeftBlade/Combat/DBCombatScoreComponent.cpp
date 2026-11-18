#include "Combat/DBCombatScoreComponent.h"

#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "GameFramework/Actor.h"

void UDBCombatScoreComponent::BeginPlay()
{
	Super::BeginPlay();
	RecentTechniques.Reserve(6);
	Health = GetOwner()->FindComponentByClass<UDBHealthComponent>();
	Combat = GetOwner()->FindComponentByClass<UDBCombatComponent>();
	if (Combat.IsValid())
	{
		Combat->OnAttackStarted.AddUObject(this, &ThisClass::AttackStarted);
		Combat->OnAttackHit.AddUObject(this, &ThisClass::AttackHit);
		Combat->OnParrySucceeded.AddUObject(this, &ThisClass::ParrySucceeded);
	}
}

void UDBCombatScoreComponent::AttackStarted()
{
	// Missing a stage breaks the run of successful links, without taking away earned points.
	if (!bCurrentAttackHit)
	{
		ChainHits = 0;
	}
	bCurrentAttackHit = false;
}

void UDBCombatScoreComponent::AttackHit(
    const UDBWeaponData* Weapon, EDBAttackId Attack, uint64 ActionId, uint64 ChainId)
{
	if (!Health.IsValid() || Health->IsDead() || !IsValid(Weapon) || ActionId == 0 || ActionId <= LastScoredActionId)
	{
		return;
	}
	const FDBAttackDefinition* Definition = Weapon->Attacks.FindByPredicate(
	    [Attack](const FDBAttackDefinition& Entry)
	    {
		    return Entry.Id == Attack;
	    });
	if (!Definition)
	{
		return;
	}
	LastScoredActionId = ActionId;
	if (LastChainId != ChainId)
	{
		ChainHits = 0;
	}
	LastChainId = ChainId;
	bCurrentAttackHit = true;
	++ChainHits;

	const bool bVaried = !RecentTechniques.ContainsByPredicate(
	    [Weapon, Attack](const FTechnique& Entry)
	    {
		    return Entry.Weapon.Get() == Weapon && Entry.Attack == Attack;
	    });
	if (RecentTechniques.Num() == 6)
	{
		RecentTechniques.RemoveAt(0, 1, EAllowShrinking::No);
	}
	RecentTechniques.Add({Weapon, Attack});
	// A chain can finish its own input while still offering a different-input branch.
	const bool bLightFinish =
	    Attack >= EDBAttackId::Light1 && Attack <= EDBAttackId::Light4 && Definition->NextLight == EDBAttackId::None;
	const bool bHeavyFinish =
	    Attack >= EDBAttackId::Heavy1 && Attack <= EDBAttackId::Heavy4 && Definition->NextHeavy == EDBAttackId::None;
	const bool bFinish =
	    ChainHits >= 2
	    && (bLightFinish || bHeavyFinish || Attack == EDBAttackId::AirLight3 || Attack == EDBAttackId::AirHeavy
	        || (Definition->NextLight == EDBAttackId::None && Definition->NextHeavy == EDBAttackId::None));
	const int64 Points = int64(FMath::Max(0, HitPoints)) + (bVaried ? FMath::Max(0, VariationBonus) : 0)
	                     + (bFinish ? FMath::Max(0, FinishBonus) : 0);
	Award(Points, bFinish ? (bVaried ? EDBScoreEvent::VariedFinish : EDBScoreEvent::Finish)
	                      : (bVaried ? EDBScoreEvent::Variation : EDBScoreEvent::Hit));
}

void UDBCombatScoreComponent::ParrySucceeded()
{
	if (Health.IsValid() && !Health->IsDead())
	{
		Award(ParryPoints, EDBScoreEvent::Parry);
	}
}

void UDBCombatScoreComponent::Award(int64 Points, EDBScoreEvent Event)
{
	if (Points <= 0)
	{
		return;
	}
	const int32 Delta = static_cast<int32>(FMath::Min(Points, int64(MAX_int32 - Score)));
	if (Delta <= 0)
	{
		return;
	}
	Score += Delta;
	OnScoreChanged.Broadcast(Delta, Event);
}

void UDBCombatScoreComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Combat.IsValid())
	{
		Combat->OnAttackStarted.RemoveAll(this);
		Combat->OnAttackHit.RemoveAll(this);
		Combat->OnParrySucceeded.RemoveAll(this);
	}
	Super::EndPlay(Reason);
}
