#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DBPlayerHUD.generated.h"

class UDBPlayerHUDWidget;

UCLASS()
class DEFTBLADE_API ADBPlayerHUD : public AHUD
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UDBPlayerHUDWidget> WidgetClass;

private:
	UFUNCTION()
	void PawnChanged(APawn* OldPawn, APawn* NewPawn);

	UPROPERTY(Transient)
	TObjectPtr<UDBPlayerHUDWidget> PlayerWidget;
};
