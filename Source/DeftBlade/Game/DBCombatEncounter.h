#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DBCombatEncounter.generated.h"

class ADBEnemyCharacter;
class ADBBossCharacter;
class ADBPlayerCharacter;
class ADBTrainingDummy;
class ATargetPoint;
class ATriggerBox;
class UBoxComponent;
class UStaticMeshComponent;
class ACameraActor;
class APlayerController;
class UAnimMontage;

UENUM(BlueprintType)
enum class EDBEncounterState : uint8
{
	Ready,
	Active,
	Cleared,
	Reinforcements,
	Completed,
	BossActive,
	BossIntroduction,
	AirReinforcement,
	ParryReinforcement
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
	const ADBTrainingDummy* GetTrainingDummy() const
	{
		return TrainingDummy;
	}
	ADBBossCharacter* GetBoss() const
	{
		return Boss;
	}
	float GetBossIntroductionProgress() const;
	const FText& GetBossIntroductionName() const
	{
		return BossIntroductionName;
	}
	const FText& GetBossIntroductionSubtitle() const
	{
		return BossIntroductionSubtitle;
	}

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
	UPROPERTY(EditInstanceOnly, Category = "Encounter|Tutorial")
	TObjectPtr<ADBTrainingDummy> TrainingDummy;
	/** An optional enemy that drops in before the side shutter opens. */
	UPROPERTY(EditInstanceOnly, Category = "Encounter|Reinforcements")
	FDBReinforcementSpawn DropReinforcement;
	/** Optional solo enemy for the parry lesson before the final group. */
	UPROPERTY(EditInstanceOnly, Category = "Encounter|Reinforcements")
	FDBReinforcementSpawn ParryReinforcement;
	/** Optional end area beyond the exit. Enabled only after combat is cleared. */
	UPROPERTY(EditInstanceOnly, Category = "Encounter")
	TObjectPtr<ATriggerBox> FinishArea;
	/** When assigned, FinishArea starts the final fight instead of completing the run. */
	UPROPERTY(EditInstanceOnly, Category = "Encounter|Boss")
	TObjectPtr<ADBBossCharacter> Boss;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter|Boss")
	TObjectPtr<UStaticMeshComponent> BossGate;
	UPROPERTY(EditAnywhere, Category = "Encounter|Boss|Introduction")
	FText BossIntroductionName = NSLOCTEXT("DeftBlade", "BossIntroductionName", "WARDEN");
	UPROPERTY(EditAnywhere, Category = "Encounter|Boss|Introduction")
	FText BossIntroductionSubtitle = NSLOCTEXT("DeftBlade", "BossIntroductionSubtitle", "IRONCLAD SENTINEL");
	UPROPERTY(EditAnywhere, Category = "Encounter|Boss|Introduction")
	TObjectPtr<UAnimMontage> BossIntroductionMontage;
	UPROPERTY(EditAnywhere, Category = "Encounter|Boss|Introduction", meta = (ClampMin = "2", Units = "s"))
	float BossIntroductionDuration = 3.6f;
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
	void BossDefeated();
	void StartReinforcements(EDBEncounterState NewState);
	ADBEnemyCharacter* SpawnReinforcement(const FDBReinforcementSpawn& Spawn, bool bDrop);
	void BindEnemy(ADBEnemyCharacter* Enemy);
	void BindPlayer(ADBPlayerCharacter* Player);
	void StartBossIntroduction();
	void UpdateBossIntroduction();
	void FinishBossIntroduction(bool bStartFight);
	void PlayerDied();
	void SetState(EDBEncounterState NewState);
	void UpdateGates();
	TWeakObjectPtr<ADBPlayerCharacter> EncounterPlayer;
	FVector GateClosedLocation = FVector::ZeroVector;
	double GateOpenStartedAt = -1;
	double BossIntroductionStartedAt = -1;
	bool bReturningFromIntroduction = false;
	TWeakObjectPtr<APlayerController> IntroductionController;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> IntroductionCamera;

	UPROPERTY(
	    VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Encounter", meta = (AllowPrivateAccess = "true"))
	EDBEncounterState State = EDBEncounterState::Ready;
};
