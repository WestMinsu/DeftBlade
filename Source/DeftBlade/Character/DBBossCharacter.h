#pragma once

#include "CoreMinimal.h"
#include "Character/DBEnemyCharacter.h"
#include "DBBossCharacter.generated.h"

UCLASS()
class DEFTBLADE_API ADBBossCharacter : public ADBEnemyCharacter
{
	GENERATED_BODY()
public:
	ADBBossCharacter();
	void Activate(APawn* Player);
	void TurnTowards(const FVector& Direction, float DeltaSeconds);
	virtual bool CanAct() const override;
	virtual bool HasStaggerResistance() const override;
	virtual bool CanBeLaunched() const override;
	virtual bool CanAttackAtDistance(float Distance) const override;
	virtual EDBAttackId SelectAttack(float Distance, bool bCombo) const override;
	virtual float TakeDamage(
	    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	/** Maximum opening after a parry. A successful launch spends it; ordinary hits never refresh it. */
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Combat", meta = (ClampMin = "0", Units = "s"))
	float ParryOpeningDuration = 2.5f;

	/** The hidden Manny mesh evaluates locomotion, slots and ragdoll once for all armour parts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TArray<TObjectPtr<USkeletalMeshComponent>> Armour;

private:
	EDBAttackId SelectApproachAttack(float Distance) const;
	void SharePose();
	void HandleAttackStarted();
	void OpenParryOpening(AActor* Defender);
	bool bActive = false;
	bool bAirComboActive = false;
	double ParryOpeningEndsAt = 0.0;
	EDBAttackId PreviousAttack = EDBAttackId::None;
};
