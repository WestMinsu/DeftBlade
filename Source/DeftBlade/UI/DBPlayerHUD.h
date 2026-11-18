#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DBPlayerHUD.generated.h"

class UDBPlayerHUDWidget;
class UDBCombatComponent;
class IInputProcessor;

UCLASS()
class DEFTBLADE_API ADBPlayerHUD : public AHUD
{
	GENERATED_BODY()
public:
	void PauseGame();
	bool ShowParryLesson(UDBCombatComponent* Attacker, const FVector& ImpactPoint);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UDBPlayerHUDWidget> WidgetClass;

private:
	void ApplicationActivationChanged(bool bActive);
	UFUNCTION()
	void PawnChanged(APawn* OldPawn, APawn* NewPawn);

	UPROPERTY(Transient)
	TObjectPtr<UDBPlayerHUDWidget> PlayerWidget;
	TSharedPtr<IInputProcessor> InputDeviceTracker;
};
