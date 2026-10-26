#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DBTargetingComponent.generated.h"

/** Attack-start assistance only; free movement and camera keep their existing policy. */
UCLASS(ClassGroup = (DeftBlade), meta = (BlueprintSpawnableComponent))
class DEFTBLADE_API UDBTargetingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDBTargetingComponent();
	AActor* SelectSoftTarget(const FVector& Direction);
	void AssistAttackFacing(const FVector& Direction);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "1"))
	float SoftTargetRange = 350.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "0"))
	float MaxHeightDifference = 120.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "1", ClampMax = "89"))
	float SoftTargetHalfAngle = 50.0f;
	/** Maximum assistance relative to the player's intended direction, per attack. */
	UPROPERTY(EditDefaultsOnly, Category = "Targeting", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxAttackYawCorrection = 35.0f;

private:
	TWeakObjectPtr<AActor> SoftTarget;
};
