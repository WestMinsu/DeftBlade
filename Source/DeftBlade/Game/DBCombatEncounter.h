#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DBCombatEncounter.generated.h"

class ADBEnemyCharacter;
class ADBPlayerCharacter;
class ATargetPoint;
class ATriggerBox;
class UBoxComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EDBEncounterState : uint8
{
	Ready,
	Active,
	Cleared,
	Reinforcements,
	Completed
};

USTRUCT(BlueprintType)
struct FDBReinforcementSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Encounter")
	TSubclassOf<ADBEnemyCharacter> EnemyClass;
	UPROPERTY(EditAnywhere, Category = "Encounter")
	TObjectPtr<ATargetPoint> SpawnPoint;
};

UCLASS()
class DEFTBLADE_API ADBCombatEncounter : public AActor
{
	GENERATED_BODY()

public:
	ADBCombatEncounter();
	virtual void Tick(float DeltaSeconds) override;

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
	/** Optional end area beyond the exit. Enabled only after combat is cleared. */
	UPROPERTY(EditInstanceOnly, Category = "Encounter")
	TObjectPtr<ATriggerBox> FinishArea;
	/** One optional group, spawned behind the side entrance after the assigned enemies fall. */
	UPROPERTY(EditInstanceOnly, Category = "Encounter|Reinforcements")
	TArray<FDBReinforcementSpawn> Reinforcements;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter|Reinforcements")
	TObjectPtr<UStaticMeshComponent> ReinforcementGate;
	UPROPERTY(EditAnywhere, Category = "Encounter|Reinforcements", meta = (ClampMin = "0.1", Units = "s"))
	float GateOpenDuration = 0.8f;
	UPROPERTY(EditAnywhere, Category = "Encounter|Reinforcements", meta = (ClampMin = "0", Units = "cm"))
	float GateLiftHeight = 450.0f;

private:
	UFUNCTION()
	void Entered(UPrimitiveComponent* Overlapped, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
	    int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION()
	void EnemyDestroyed(AActor* Enemy);
	UFUNCTION()
	void FinishEntered(AActor* Area, AActor* OtherActor);
	void TryStart(AActor* Actor);
	void CheckCleared();
	void StartReinforcements();
	void BindEnemy(ADBEnemyCharacter* Enemy);
	void PlayerDied();
	void SetState(EDBEncounterState NewState);
	void UpdateGates();
	TWeakObjectPtr<ADBPlayerCharacter> EncounterPlayer;
	FVector GateClosedLocation = FVector::ZeroVector;
	double GateOpenStartedAt = -1;

	UPROPERTY(
	    VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Encounter", meta = (AllowPrivateAccess = "true"))
	EDBEncounterState State = EDBEncounterState::Ready;
};
