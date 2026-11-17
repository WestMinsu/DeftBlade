#include "UI/DBPlayerHUDWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Combat/DBHealthComponent.h"
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

void UDBPlayerHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RetryButton->OnClicked.AddDynamic(this, &ThisClass::Retry);
	ResumeButton->OnClicked.AddDynamic(this, &ThisClass::ResumeGame);
	RestartButton->OnClicked.AddDynamic(this, &ThisClass::Retry);
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
	RetryButton->SetIsEnabled(true);
	RestartButton->SetIsEnabled(true);
	HealthSource = Health;
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
		case EDBEncounterState::Cleared:
			EncounterText->SetText(EncounterSource->GetBoss()
			                           ? NSLOCTEXT("DeftBlade", "BossAhead", "AREA CLEAR - Proceed to the Warden")
			                           : NSLOCTEXT("DeftBlade", "EncounterCleared", "AREA CLEAR - Exit unlocked"));
			break;
		case EDBEncounterState::BossActive:
			EncounterText->SetVisibility(ESlateVisibility::Collapsed);
			break;
		case EDBEncounterState::Completed:
			EncounterText->SetVisibility(ESlateVisibility::Collapsed);
			break;
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

FReply UDBPlayerHUDWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Screen != EScreen::Gameplay
	    && (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::Gamepad_Special_Right))
	{
		if (Screen == EScreen::Pause && !Event.IsRepeat())
		{
			ResumeGame();
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
	const bool bPaused = Screen == EScreen::Pause;
	Screen = EScreen::Gameplay;
	PausePanel->SetVisibility(ESlateVisibility::Collapsed);
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
	if (Screen != EScreen::Gameplay)
	{
		APlayerController* PC = GetOwningPlayer();
		const bool bMenuFocused = Screen == EScreen::Pause
		                              ? ResumeButton->HasUserFocus(PC) || RestartButton->HasUserFocus(PC)
		                              : RetryButton->HasUserFocus(PC);
		// Opening the panel or clicking its background can leave Slate without a focused button.
		if (PC && !bMenuFocused)
		{
			(Screen == EScreen::Pause ? ResumeButton : RetryButton)->SetUserFocus(PC);
		}
		for (UButton* Button : {ResumeButton.Get(), RestartButton.Get(), RetryButton.Get()})
		{
			const bool bSelected = Button->HasUserFocus(GetOwningPlayer()) || Button->IsHovered();
			Button->SetBackgroundColor(
			    bSelected ? FLinearColor(0.33f, 0.035f, 0.055f) : FLinearColor(0.025f, 0.03f, 0.037f));
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
