#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DBPlayerHUDWidget.generated.h"

class UBorder;
class ADBCombatEncounter;
class UButton;
class UDBHealthComponent;
class UDBCombatComponent;
class UDBCombatScoreComponent;
enum class EDBScoreEvent : uint8;
class UDBTargetingComponent;
class UProgressBar;
class UTextBlock;
class UWidgetAnimation;
class UDBSettingsWidget;

UCLASS(Abstract)
class DEFTBLADE_API UDBPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void BindHealth(UDBHealthComponent* Health);
	void BindEncounter(ADBCombatEncounter* Encounter);
	void PauseGame();
	void SetUsingGamepad(bool bGamepad);
	bool HandleTutorialInput(const FKey& Key, bool bRepeat);
	bool ShowParryLesson(UDBCombatComponent* Attacker, const FVector& ImpactPoint);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ScorePanel;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreEventText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultScoreText;
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
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BossPanel;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> BossHealthBar;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> HealthPanel;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BossIntroductionPanel;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BossIntroductionCard;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> TutorialShade;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> TutorialCard;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialTitle;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialBody;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialInput;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BossIntroductionName;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BossIntroductionSubtitle;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> PausePanel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ResumeButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RestartButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SettingsButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UDBSettingsWidget> SettingsPanel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> RetryHint;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> PauseHint;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> GamepadRetryHint;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> GamepadPauseHint;

private:
	void HealthChanged(float Current, float Maximum);
	void ScoreChanged(int32 Points, EDBScoreEvent Event);
	void EncounterChanged();
	void UpdateTutorial();
	void RenderTutorial();
	void UpdateBossIntroduction();
	void BossHealthChanged(float Current, float Maximum);
	void ShowDeath();
	void ShowResult(bool bCompleted);
	void ReleaseMenuInput();
	UFUNCTION()
	void OpenSettings();
	void CloseSettings();
	UFUNCTION()
	void ResumeGame();
	UFUNCTION()
	void Retry();

	TWeakObjectPtr<UDBHealthComponent> HealthSource;
	TWeakObjectPtr<UDBCombatScoreComponent> ScoreSource;
	double ScoreEventUntil = 0.0;
	TWeakObjectPtr<ADBCombatEncounter> EncounterSource;
	TWeakObjectPtr<UDBHealthComponent> BossHealthSource;
	TWeakObjectPtr<UDBTargetingComponent> TargetingSource;
	float PreviousHealth = 0;
	enum class EScreen : uint8
	{
		Gameplay,
		Pause,
		Settings,
		Result,
		Tutorial
	};
	EScreen Screen = EScreen::Gameplay;
	bool bRetryRequested = false;
	bool bUsingGamepad = false;

	enum class ETutorialHint : uint8
	{
		None,
		Movement,
		Jump,
		DoubleJump,
		EvadeSprint,
		Practice,
		PracticeCombos,
		MixedCombo,
		AirCombo,
		Checkpoint,
		Parry
	};
	ETutorialHint TutorialHint = ETutorialHint::None;
	double TutorialStageStartedAt = 0.0;
	double ParryInputAllowedAt = 0.0;
	bool bParryInputReady = false;
	bool bReachedContextLesson = false;
	bool bParryTaught = false;
	TWeakObjectPtr<UDBCombatComponent> TutorialAttacker;
	FVector TutorialImpactPoint = FVector::ZeroVector;
	uint64 TutorialAttackId = 0;
};
