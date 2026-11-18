#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DBTrainingDummy.generated.h"

class UCapsuleComponent;
class UDBHealthComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class DEFTBLADE_API ADBTrainingDummy : public AActor
{
	GENERATED_BODY()

public:
	ADBTrainingDummy();
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
	    AActor* DamageCauser) override;
	bool IsRepeatablePractice() const
	{
		return bRepeatablePractice;
	}
	bool HasPracticeHit() const
	{
		return bPracticeHit;
	}

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Training")
	TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Training")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Training")
	TObjectPtr<UTextRenderComponent> HealthText;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Training")
	TObjectPtr<UDBHealthComponent> HealthComponent;
	UPROPERTY(EditInstanceOnly, Category = "Training")
	bool bRepeatablePractice = false;

private:
	void UpdateHealthText(float Current, float Maximum);
	void HandleDeath();
	void RestoreColor();
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	FTimerHandle FeedbackTimer;
	bool bPracticeHit = false;
};
