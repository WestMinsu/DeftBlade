#include "UI/DBPlayerHUDWidget.h"

#include "Components/Button.h"
#include "Game/DBGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UDBPlayerHUDWidget::ShowTitle()
{
	auto* PC = GetOwningPlayer();
	if (!PC || !TitlePanel || !TitleMenu || !StartGameButton || !TitleSettingsButton || !QuitGameButton)
	{
		return;
	}

	Screen = EScreen::Title;
	TitlePanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TitleMenu->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	PC->FlushPressedKeys();
	PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(StartGameButton->TakeWidget()));
	PC->bShowMouseCursor = true;
	StartGameButton->SetUserFocus(PC);
}

void UDBPlayerHUDWidget::StartGame()
{
	if (Screen != EScreen::Title || !TitleMenu->GetIsEnabled() || TitleStartMap.IsNull())
	{
		return;
	}

	TitleMenu->SetIsEnabled(false);
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, TitleStartMap);
}

void UDBPlayerHUDWidget::QuitGame()
{
	const bool bTitle = Screen == EScreen::Title && TitleMenu->GetIsEnabled();
	if (!bLevelTravelRequested && (bTitle || Screen == EScreen::Pause || Screen == EScreen::Result))
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
	}
}

void UDBPlayerHUDWidget::ReturnToTitle()
{
	if (bLevelTravelRequested || (Screen != EScreen::Pause && Screen != EScreen::Result))
	{
		return;
	}

	bLevelTravelRequested = true;
	ReleaseMenuInput();
	UGameplayStatics::OpenLevel(this, FName(ADBGameMode::TitleMapPath));
}

void UDBPlayerHUDWidget::UpdateTitleSelection()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC || !TitleMenu->GetIsEnabled())
	{
		return;
	}
	if (!StartGameButton->HasUserFocus(PC) && !TitleSettingsButton->HasUserFocus(PC)
	    && !QuitGameButton->HasUserFocus(PC))
	{
		StartGameButton->SetUserFocus(PC);
	}
	const bool bHovering =
	    !bUsingGamepad
	    && (StartGameButton->IsHovered() || TitleSettingsButton->IsHovered() || QuitGameButton->IsHovered());
	for (UButton* Button : {StartGameButton.Get(), TitleSettingsButton.Get(), QuitGameButton.Get()})
	{
		const bool bSelected = bHovering ? Button->IsHovered() : Button->HasUserFocus(PC);
		Button->SetBackgroundColor(
		    bSelected ? FLinearColor(0.26f, 0.08f, 0.14f) : FLinearColor(0.88f, 0.81f, 0.79f, 0.90f));
		Button->SetColorAndOpacity(bSelected ? FLinearColor(1.0f, 0.95f, 0.92f) : FLinearColor(0.052f, 0.027f, 0.040f));
	}
}
