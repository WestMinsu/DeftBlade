#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DBPlayerHUDWidget.generated.h"

class UBorder;
class ADBCombatEncounter;
class UButton;
class UDBHealthComponent;
class UDBTargetingComponent;
class UProgressBar;
class UTextBlock;
class UWidgetAnimation;

UCLASS(Abstract)
class DEFTBLADE_API UDBPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void BindHealth(UDBHealthComponent* Health);
	void BindEncounter(ADBCombatEncounter* Encounter);
	void PauseGame();
	void RequestMenuFocus()
	{
		bRestoreMenuFocus = true;
	}

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HealthText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> DamageFlash;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> ResultPanel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ResultTitle;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RetryLabel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient, meta = (BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> DamageFlashAnimation;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> LockOnMarker;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EncounterText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> PausePanel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ResumeButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RestartButton;

private:
	void HealthChanged(float Current, float Maximum);
	void EncounterChanged();
	void ShowDeath();
	void ShowResult(bool bCompleted);
	void ReleaseMenuInput();
	UFUNCTION()
	void ResumeGame();
	UFUNCTION()
	void Retry();

	TWeakObjectPtr<UDBHealthComponent> HealthSource;
	TWeakObjectPtr<ADBCombatEncounter> EncounterSource;
	TWeakObjectPtr<UDBTargetingComponent> TargetingSource;
	float PreviousHealth = 0;
	enum class EScreen : uint8
	{
		Gameplay,
		Pause,
		Result
	};
	EScreen Screen = EScreen::Gameplay;
	bool bRetryRequested = false;
	bool bRestoreMenuFocus = false;
};
