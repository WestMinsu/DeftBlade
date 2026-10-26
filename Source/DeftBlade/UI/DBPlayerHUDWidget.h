#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DBPlayerHUDWidget.generated.h"

class UBorder;
class UButton;
class UDBHealthComponent;
class UProgressBar;
class UTextBlock;
class UWidgetAnimation;

UCLASS(Abstract)
class DEFTBLADE_API UDBPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void BindHealth(UDBHealthComponent* Health);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HealthText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> DamageFlash;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> DeathPanel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient, meta = (BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> DamageFlashAnimation;

private:
	void HealthChanged(float Current, float Maximum);
	void ShowDeath();
	void ReleaseDeathInput();
	UFUNCTION()
	void Retry();

	TWeakObjectPtr<UDBHealthComponent> HealthSource;
	float PreviousHealth = 0;
	bool bShowingDeath = false;
	bool bRetryRequested = false;
};
