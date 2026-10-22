#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DBCombatComponent.generated.h"

UENUM(BlueprintType)
enum class EDBActionState : uint8
{
	None,
	Evade
};

/** One deliberate press. Id == 0 means the slot is empty. */
USTRUCT()
struct FDBBufferedEvade
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	uint64 Id = 0;

	UPROPERTY(VisibleAnywhere)
	double ReceivedAt = 0.0;

	bool IsExpired(double Now, double Lifetime) const
	{
		return Id != 0 && Now >= ReceivedAt + Lifetime;
	}
};

/** M2a: action lifetime and one buffered evade; movement is added in M2b. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDBCombatComponent();

	void RequestEvade();
	bool FinishAction(uint64 ExpectedActionId);
	void ResetCombat();

	EDBActionState GetActionState() const { return ActionState; }
	uint64 GetActiveActionId() const { return ActiveActionId; }
	const FDBBufferedEvade& GetBufferedEvade() const { return BufferedEvade; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double InputBufferLifetime = 0.18;

	/** State-only duration until M2b supplies the actual evade movement. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	double EvadeDuration = 0.32;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Combat")
	EDBActionState ActionState = EDBActionState::None;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Combat")
	uint64 ActiveActionId = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Combat")
	FDBBufferedEvade BufferedEvade;

private:
	bool CanProcessInput() const;
	void TryStartEvade(double Now);

	double ActionEndsAt = 0.0;
	uint64 NextRequestId = 0;
};
