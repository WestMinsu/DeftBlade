#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DBCombatEncounter.generated.h"

class ADBEnemyCharacter;
class UBoxComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EDBEncounterState : uint8
{
	Ready,
	Active,
	Cleared
};

UCLASS()
class DEFTBLADE_API ADBCombatEncounter : public AActor
{
	GENERATED_BODY()

public:
	ADBCombatEncounter();

	UFUNCTION(BlueprintPure, Category = "Encounter")
	EDBEncounterState GetState() const
	{
		return State;
	}

	FSimpleMulticastDelegate OnStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<UBoxComponent> EntryTrigger;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<UStaticMeshComponent> EntranceGate;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter")
	TObjectPtr<UStaticMeshComponent> ExitGate;
	UPROPERTY(EditInstanceOnly, Category = "Encounter")
	TArray<TObjectPtr<ADBEnemyCharacter>> Enemies;

private:
	UFUNCTION()
	void Entered(UPrimitiveComponent* Overlapped, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
	    int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION()
	void EnemyDestroyed(AActor* Enemy);
	void TryStart(AActor* Actor);
	void CheckCleared();
	void SetState(EDBEncounterState NewState);
	void UpdateGates();

	UPROPERTY(
	    VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Encounter", meta = (AllowPrivateAccess = "true"))
	EDBEncounterState State = EDBEncounterState::Ready;
};
