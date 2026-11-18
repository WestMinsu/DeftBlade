#include "UI/DBPlayerHUDWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBCombatScoreComponent.h"
#include "Engine/World.h"
#include "Combat/DBTargetingComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Game/DBCombatEncounter.h"
#include "Character/DBBossCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Input/Events.h"
#include "Kismet/GameplayStatics.h"
#include "UI/DBSettingsWidget.h"

void UDBPlayerHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (TutorialCard)
	{
		SetIsFocusable(true);
	}
	RetryButton->OnClicked.AddDynamic(this, &ThisClass::Retry);
	ResumeButton->OnClicked.AddDynamic(this, &ThisClass::ResumeGame);
	RestartButton->OnClicked.AddDynamic(this, &ThisClass::Retry);
	SettingsButton->OnClicked.AddDynamic(this, &ThisClass::OpenSettings);
	SettingsPanel->OnBackRequested.BindUObject(this, &ThisClass::CloseSettings);
	SetUsingGamepad(false);
}

void UDBPlayerHUDWidget::SetUsingGamepad(bool bGamepad)
{
	const ESlateVisibility KeyboardVisibility =
	    bGamepad ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
	const ESlateVisibility GamepadVisibility =
	    bGamepad ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	RetryHint->SetVisibility(KeyboardVisibility);
	PauseHint->SetVisibility(KeyboardVisibility);
	GamepadRetryHint->SetVisibility(GamepadVisibility);
	GamepadPauseHint->SetVisibility(GamepadVisibility);
	SettingsPanel->SetUsingGamepad(bGamepad);
	if (bUsingGamepad != bGamepad)
	{
		bUsingGamepad = bGamepad;
		RenderTutorial();
	}
}

void UDBPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	APawn* Pawn = GetOwningPlayerPawn();
	BindHealth(Pawn ? Pawn->FindComponentByClass<UDBHealthComponent>() : nullptr);
}

void UDBPlayerHUDWidget::BindHealth(UDBHealthComponent* Health)
{
	if (Health && HealthSource.Get() == Health)
	{
		return;
	}
	if (HealthSource.IsValid())
	{
		HealthSource->OnHealthChanged.RemoveAll(this);
		HealthSource->OnDeath.RemoveAll(this);
	}
	ReleaseMenuInput();
	bRetryRequested = false;
	StopAnimation(DamageFlashAnimation);
	DamageFlash->SetRenderOpacity(0);
	ResultPanel->SetVisibility(ESlateVisibility::Collapsed);
	PausePanel->SetVisibility(ESlateVisibility::Collapsed);
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	RetryButton->SetIsEnabled(true);
	RestartButton->SetIsEnabled(true);
	HealthSource = Health;
	if (ScoreSource.IsValid())
	{
		ScoreSource->OnScoreChanged.RemoveAll(this);
	}
	ScoreSource = Health ? Health->GetOwner()->FindComponentByClass<UDBCombatScoreComponent>() : nullptr;
	if (ScoreSource.IsValid())
	{
		ScoreSource->OnScoreChanged.AddUObject(this, &ThisClass::ScoreChanged);
	}
	ScoreChanged(0, EDBScoreEvent::Hit);
	TargetingSource = Health ? Health->GetOwner()->FindComponentByClass<UDBTargetingComponent>() : nullptr;
	LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	// An initial snapshot is not damage, even when binding to an already wounded pawn.
	PreviousHealth = Health ? Health->GetHealth() : 0;
	HealthChanged(PreviousHealth, Health ? Health->GetMaxHealth() : 0);
	if (Health)
	{
		Health->OnHealthChanged.AddUObject(this, &ThisClass::HealthChanged);
		Health->OnDeath.AddUObject(this, &ThisClass::ShowDeath);
		if (Health->IsDead())
		{
			ShowDeath();
		}
	}
}

void UDBPlayerHUDWidget::HealthChanged(float Current, float Maximum)
{
	HealthBar->SetPercent(Maximum > 0 ? FMath::Clamp(Current / Maximum, 0.f, 1.f) : 0);
	if (Current < PreviousHealth)
	{
		StopAnimation(DamageFlashAnimation);
		PlayAnimation(DamageFlashAnimation);
	}
	PreviousHealth = Current;
}

void UDBPlayerHUDWidget::ScoreChanged(int32 Points, EDBScoreEvent Event)
{
	const FText Value = FText::AsNumber(ScoreSource.IsValid() ? ScoreSource->GetScore() : 0);
	if (ScoreText)
	{
		ScoreText->SetText(Value);
	}
	if (ResultScoreText)
	{
		// The last killing contact can be confirmed after the result panel has opened.
		ResultScoreText->SetText(FText::Format(NSLOCTEXT("DeftBlade", "ResultScore", "TACTICAL SCORE   {0}"), Value));
	}
	if (!ScoreEventText)
	{
		return;
	}
	if (Points <= 0)
	{
		ScoreEventUntil = 0.0;
		ScoreEventText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	FText Label;
	switch (Event)
	{
		case EDBScoreEvent::Variation:
			Label = NSLOCTEXT("DeftBlade", "ScoreVariation", "NEW TECHNIQUE");
			break;
		case EDBScoreEvent::Finish:
			Label = NSLOCTEXT("DeftBlade", "ScoreFinish", "CHAIN FINISH");
			break;
		case EDBScoreEvent::VariedFinish:
			Label = NSLOCTEXT("DeftBlade", "ScoreVariedFinish", "VARIED FINISH");
			break;
		case EDBScoreEvent::Parry:
			Label = NSLOCTEXT("DeftBlade", "ScoreParry", "PARRY");
			break;
		default:
			Label = NSLOCTEXT("DeftBlade", "ScoreHit", "STRIKE");
			break;
	}
	ScoreEventText->SetText(
	    FText::Format(NSLOCTEXT("DeftBlade", "ScoreAward", "{0}  +{1}"), Label, FText::AsNumber(Points)));
	ScoreEventText->SetColorAndOpacity(FSlateColor(
	    Event == EDBScoreEvent::Parry                                            ? FLinearColor(.95f, .72f, .25f)
	    : Event == EDBScoreEvent::Finish || Event == EDBScoreEvent::VariedFinish ? FLinearColor(.4f, .86f, .75f)
	                                                                             : FLinearColor(.7f, .7f, .68f)));
	ScoreEventText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ScoreEventText->SetRenderOpacity(1.f);
	ScoreEventUntil = GetWorld()->GetTimeSeconds() + 2.0;
}

void UDBPlayerHUDWidget::BindEncounter(ADBCombatEncounter* Encounter)
{
	if (BossHealthSource.IsValid())
	{
		BossHealthSource->OnHealthChanged.RemoveAll(this);
	}
	BossHealthSource = Encounter && IsValid(Encounter->GetBoss())
	                       ? Encounter->GetBoss()->FindComponentByClass<UDBHealthComponent>()
	                       : nullptr;
	if (BossHealthSource.IsValid())
	{
		BossHealthSource->OnHealthChanged.AddUObject(this, &ThisClass::BossHealthChanged);
		BossHealthChanged(BossHealthSource->GetHealth(), BossHealthSource->GetMaxHealth());
	}
	if (EncounterSource.IsValid())
	{
		EncounterSource->OnStateChanged.RemoveAll(this);
	}
	EncounterSource = Encounter;
	if (Encounter)
	{
		Encounter->OnStateChanged.AddUObject(this, &ThisClass::EncounterChanged);
	}
	EncounterChanged();
}

void UDBPlayerHUDWidget::EncounterChanged()
{
	TutorialStageStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UpdateBossIntroduction();
	if (BossPanel)
	{
		BossPanel->SetVisibility(
		    EncounterSource.IsValid() && EncounterSource->GetState() == EDBEncounterState::BossActive
		        ? ESlateVisibility::HitTestInvisible
		        : ESlateVisibility::Collapsed);
	}
	if (EncounterSource.IsValid() && EncounterSource->GetState() == EDBEncounterState::Completed)
	{
		ShowResult(true);
	}
	if (!EncounterText)
	{
		return;
	}
	EncounterText->SetVisibility(
	    EncounterSource.IsValid() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!EncounterSource.IsValid())
	{
		return;
	}
	switch (EncounterSource->GetState())
	{
		case EDBEncounterState::Ready:
			EncounterText->SetText(NSLOCTEXT("DeftBlade", "EncounterReady", "Enter the combat area"));
			break;
		case EDBEncounterState::Active:
			EncounterText->SetText(NSLOCTEXT("DeftBlade", "EncounterActive", "Defeat all enemies"));
			break;
		case EDBEncounterState::Reinforcements:
			EncounterText->SetText(
			    NSLOCTEXT("DeftBlade", "EncounterReinforcements", "REINFORCEMENTS - Defeat the incoming enemies"));
			break;
		case EDBEncounterState::AirReinforcement:
			EncounterText->SetText(
			    NSLOCTEXT("DeftBlade", "AirReinforcement", "REINFORCEMENT - Defeat the incoming enemy"));
			break;
		case EDBEncounterState::Cleared:
			EncounterText->SetText(EncounterSource->GetBoss()
			                           ? NSLOCTEXT("DeftBlade", "BossAhead", "AREA CLEAR - Proceed to the Warden")
			                           : NSLOCTEXT("DeftBlade", "EncounterCleared", "AREA CLEAR - Exit unlocked"));
			break;
		case EDBEncounterState::BossIntroduction:
		case EDBEncounterState::BossActive:
			EncounterText->SetVisibility(ESlateVisibility::Collapsed);
			break;
		case EDBEncounterState::Completed:
			EncounterText->SetVisibility(ESlateVisibility::Collapsed);
			break;
	}
}

void UDBPlayerHUDWidget::UpdateBossIntroduction()
{
	const float Progress = EncounterSource.IsValid() ? EncounterSource->GetBossIntroductionProgress() : -1.f;
	const bool bIntroducing = Progress >= 0 && Screen == EScreen::Gameplay;
	if (ScorePanel)
	{
		ScorePanel->SetVisibility(!bIntroducing && Screen == EScreen::Gameplay && ScoreSource.IsValid()
		                              ? ESlateVisibility::HitTestInvisible
		                              : ESlateVisibility::Collapsed);
	}
	if (HealthPanel)
	{
		HealthPanel->SetVisibility(bIntroducing ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (!BossIntroductionPanel)
	{
		return;
	}
	BossIntroductionPanel->SetVisibility(
	    bIntroducing ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bIntroducing)
	{
		return;
	}
	BossIntroductionPanel->SetRenderOpacity(FMath::Min3(Progress / .08f, 1.f, (1.f - Progress) / .15f));
	if (BossIntroductionCard)
	{
		const float Reveal = FMath::SmoothStep(.16f, .28f, Progress);
		BossIntroductionCard->SetRenderOpacity(Reveal * (1.f - FMath::SmoothStep(.72f, .85f, Progress)));
		BossIntroductionCard->SetRenderTranslation(FVector2D(FMath::Lerp(-60.f, 0.f, Reveal), 0));
	}
	if (BossIntroductionName)
	{
		BossIntroductionName->SetText(EncounterSource->GetBossIntroductionName());
	}
	if (BossIntroductionSubtitle)
	{
		BossIntroductionSubtitle->SetText(EncounterSource->GetBossIntroductionSubtitle());
	}
}

void UDBPlayerHUDWidget::ShowDeath()
{
	ShowResult(false);
}

void UDBPlayerHUDWidget::BossHealthChanged(float Current, float Maximum)
{
	if (BossHealthBar)
	{
		BossHealthBar->SetPercent(Maximum > 0.0f ? FMath::Clamp(Current / Maximum, 0.0f, 1.0f) : 0.0f);
	}
}

void UDBPlayerHUDWidget::ShowResult(bool bCompleted)
{
	if (Screen == EScreen::Result)
	{
		return;
	}
	ReleaseMenuInput();
	Screen = EScreen::Result;
	ResultTitle->SetText(bCompleted ? NSLOCTEXT("DeftBlade", "RunComplete", "TRIAL COMPLETE")
	                                : NSLOCTEXT("DeftBlade", "Defeated", "DEFEATED"));
	RetryLabel->SetText(
	    bCompleted ? NSLOCTEXT("DeftBlade", "PlayAgain", "PLAY AGAIN") : NSLOCTEXT("DeftBlade", "Retry", "RETRY"));
	ResultPanel->SetVisibility(ESlateVisibility::Visible);
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->FlushPressedKeys();
		PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(RetryButton->TakeWidget()));
		PC->bShowMouseCursor = true;
		RetryButton->SetUserFocus(PC);
	}
}

void UDBPlayerHUDWidget::PauseGame()
{
	auto* PC = GetOwningPlayer();
	if (Screen != EScreen::Gameplay || !PC || !HealthSource.IsValid() || HealthSource->IsDead())
	{
		return;
	}
	if (auto* Player = Cast<ADBPlayerCharacter>(PC->GetPawn()))
	{
		Player->ResetGameplayInput();
	}
	if (!PC->SetPause(true))
	{
		return;
	}
	Screen = EScreen::Pause;
	PausePanel->SetVisibility(ESlateVisibility::Visible);
	PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(ResumeButton->TakeWidget()));
	PC->bShowMouseCursor = true;
	ResumeButton->SetUserFocus(PC);
}

void UDBPlayerHUDWidget::ResumeGame()
{
	if (Screen == EScreen::Pause)
	{
		ReleaseMenuInput();
	}
}

void UDBPlayerHUDWidget::OpenSettings()
{
	if (Screen != EScreen::Pause)
	{
		return;
	}
	Screen = EScreen::Settings;
	PausePanel->SetVisibility(ESlateVisibility::Collapsed);
	SettingsPanel->RefreshValues();
	SettingsPanel->SetVisibility(ESlateVisibility::Visible);
	SettingsPanel->FocusFirstControl();
}

void UDBPlayerHUDWidget::CloseSettings()
{
	if (Screen != EScreen::Settings)
	{
		return;
	}
	SettingsPanel->Save();
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	Screen = EScreen::Pause;
	PausePanel->SetVisibility(ESlateVisibility::Visible);
	SettingsButton->SetUserFocus(GetOwningPlayer());
}

FReply UDBPlayerHUDWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Screen != EScreen::Gameplay
	    && (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::Gamepad_Special_Right
	        || Event.GetKey() == EKeys::Gamepad_FaceButton_Right))
	{
		if (!Event.IsRepeat())
		{
			if (Screen == EScreen::Settings)
			{
				CloseSettings();
			}
			else if (Screen == EScreen::Pause)
			{
				ResumeGame();
			}
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UDBPlayerHUDWidget::ReleaseMenuInput()
{
	if (Screen == EScreen::Gameplay)
	{
		return;
	}
	const bool bPaused = Screen == EScreen::Pause || Screen == EScreen::Settings || Screen == EScreen::Tutorial;
	if (Screen == EScreen::Settings)
	{
		SettingsPanel->Save();
	}
	Screen = EScreen::Gameplay;
	TutorialAttacker.Reset();
	TutorialAttackId = 0;
	UpdateTutorial();
	PausePanel->SetVisibility(ESlateVisibility::Collapsed);
	SettingsPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (auto* Player = Cast<ADBPlayerCharacter>(PC->GetPawn()))
		{
			Player->ResetGameplayInput();
		}
		if (bPaused)
		{
			PC->SetPause(false);
		}
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		PC->FlushPressedKeys();
	}
}

void UDBPlayerHUDWidget::Retry()
{
	const bool bCompleted = EncounterSource.IsValid() && EncounterSource->GetState() == EDBEncounterState::Completed;
	if (bRetryRequested || !HealthSource.IsValid()
	    || (Screen != EScreen::Pause && !HealthSource->IsDead() && !bCompleted))
	{
		return;
	}
	bRetryRequested = true;
	RetryButton->SetIsEnabled(false);
	RestartButton->SetIsEnabled(false);
	ReleaseMenuInput();
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

void UDBPlayerHUDWidget::NativeDestruct()
{
	BindEncounter(nullptr);
	BindHealth(nullptr);
	Super::NativeDestruct();
}

void UDBPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateBossIntroduction();
	UpdateTutorial();
	if (Screen == EScreen::Tutorial)
	{
		if (auto* PC = GetOwningPlayer(); PC && !HasUserFocus(PC))
		{
			SetUserFocus(PC);
		}
	}
	else if (Screen != EScreen::Gameplay)
	{
		APlayerController* PC = GetOwningPlayer();
		const bool bMenuFocused =
		    Screen == EScreen::Settings ? SettingsPanel->HasUserFocusedDescendants(PC)
		    : Screen == EScreen::Pause
		        ? ResumeButton->HasUserFocus(PC) || RestartButton->HasUserFocus(PC) || SettingsButton->HasUserFocus(PC)
		        : RetryButton->HasUserFocus(PC);
		// Opening the panel or clicking its background can leave Slate without a focused button.
		if (PC && !bMenuFocused)
		{
			if (Screen == EScreen::Settings)
			{
				SettingsPanel->FocusFirstControl();
			}
			else
			{
				(Screen == EScreen::Pause ? ResumeButton : RetryButton)->SetUserFocus(PC);
			}
		}
		for (UButton* Button : {ResumeButton.Get(), RestartButton.Get(), SettingsButton.Get(), RetryButton.Get()})
		{
			const bool bSelected = Button->HasUserFocus(GetOwningPlayer()) || Button->IsHovered();
			Button->SetBackgroundColor(
			    bSelected ? FLinearColor(0.33f, 0.035f, 0.055f) : FLinearColor(0.025f, 0.03f, 0.037f));
		}
	}
	if (ScoreEventText && ScoreEventUntil > 0.0)
	{
		const double Remaining = ScoreEventUntil - GetWorld()->GetTimeSeconds();
		ScoreEventText->SetRenderOpacity(FMath::Clamp(Remaining / .35, 0., 1.));
		if (Remaining <= 0.0)
		{
			ScoreEventText->SetVisibility(ESlateVisibility::Collapsed);
			ScoreEventUntil = 0.0;
		}
	}
	FVector2D Position;
	if (Screen == EScreen::Gameplay && TargetingSource.IsValid() && TargetingSource->IsLockedOn()
	    && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
	        GetOwningPlayer(), TargetingSource->GetTargetPoint(), Position, true))
	{
		CastChecked<UCanvasPanelSlot>(LockOnMarker->Slot)->SetPosition(Position);
		LockOnMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		LockOnMarker->SetVisibility(ESlateVisibility::Collapsed);
	}
}
