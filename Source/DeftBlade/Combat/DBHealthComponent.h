#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DBHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FDBHealthChanged, float, float);

UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBHealthComponent : public UActorComponent
{

	GENERATED_BODY()

public:
	/** Returns accepted HP loss; invalid damage, death and evade i-frames return zero. */
	float ApplyDamage(float Amount);

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const { return Health; }
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return Health <= 0.0f; }

	FDBHealthChanged OnHealthChanged;
	FSimpleMulticastDelegate OnDeath;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "1"))
	float MaxHealth = 100.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Health")
	float Health = 100.0f;
};
