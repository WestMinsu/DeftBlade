#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/DBWeaponData.h"
#include "DBCombatScoreComponent.generated.h"

class UDBCombatComponent;
class UDBHealthComponent;

UENUM()
enum class EDBScoreEvent : uint8
{
	Hit,
	Variation,
	Finish,
	VariedFinish,
	Parry
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDBScoreChanged, int32, EDBScoreEvent);

/** A run total earned from confirmed contacts, varied techniques and successful parries. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBCombatScoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetScore() const
	{
		return Score;
	}
	FDBScoreChanged OnScoreChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Score", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 HitPoints = 10;
	UPROPERTY(EditDefaultsOnly, Category = "Score", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 VariationBonus = 20;
	UPROPERTY(EditDefaultsOnly, Category = "Score", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 FinishBonus = 80;
	UPROPERTY(EditDefaultsOnly, Category = "Score", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 ParryPoints = 150;

private:
	void AttackStarted();
	void AttackHit(const UDBWeaponData* Weapon, EDBAttackId Attack, uint64 ActionId, uint64 ChainId);
	void ParrySucceeded();
	void Award(int64 Points, EDBScoreEvent Event);

	struct FTechnique
	{
		TWeakObjectPtr<const UDBWeaponData> Weapon;
		EDBAttackId Attack;
	};
	TArray<FTechnique> RecentTechniques;
	TWeakObjectPtr<UDBCombatComponent> Combat;
	TWeakObjectPtr<UDBHealthComponent> Health;
	uint64 LastScoredActionId = 0;
	uint64 LastChainId = 0;
	int32 ChainHits = 0;
	bool bCurrentAttackHit = false;
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Score")
	int32 Score = 0;
};
