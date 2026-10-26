#include "Combat/DBHealthComponent.h"

#include "Combat/DBCombatComponent.h"
#include "GameFramework/Actor.h"

void UDBHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	MaxHealth = FMath::IsFinite(MaxHealth) && MaxHealth > 0.0f ? MaxHealth : 100.0f;
	Health = MaxHealth;
}

float UDBHealthComponent::ApplyDamage(float Amount)
{
	if (!FMath::IsFinite(Amount) || Amount <= 0.0f || IsDead() || !GetOwner()->CanBeDamaged())
	{
		return 0.0f;
	}
	const UDBCombatComponent* Combat = GetOwner()->FindComponentByClass<UDBCombatComponent>();
	if (Combat && Combat->IsInvulnerable())
	{
		return 0.0f;
	}

	const float Applied = FMath::Min(Amount, Health);
	Health = FMath::Max(0.0f, Health - Applied);
	const bool bDied = IsDead();
	OnHealthChanged.Broadcast(Health, MaxHealth);
	if (bDied)
	{
		OnDeath.Broadcast();
	}
	return Applied;
}
