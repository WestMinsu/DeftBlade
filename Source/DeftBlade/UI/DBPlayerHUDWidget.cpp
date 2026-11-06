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
#include "Kismet/GameplayStatics.h"

void UDBPlayerHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	RetryButton->OnClicked.AddDynamic(this, &ThisClass::Retry);
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
	ReleaseDeathInput();
	bRetryRequested = false;
	StopAnimation(DamageFlashAnimation);
	DamageFlash->SetRenderOpacity(0);
	DeathPanel->SetVisibility(ESlateVisibility::Collapsed);
	RetryButton->SetIsEnabled(true);
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
	HealthText->SetText(FText::Format(NSLOCTEXT("DeftBlade", "PlayerHP", "{0} / {1}"),
	    FText::AsNumber(FMath::CeilToInt(Current)), FText::AsNumber(FMath::CeilToInt(Maximum))));
	if (Current < PreviousHealth)
	{
		StopAnimation(DamageFlashAnimation);
		PlayAnimation(DamageFlashAnimation);
	}
	PreviousHealth = Current;
}

void UDBPlayerHUDWidget::BindEncounter(ADBCombatEncounter* Encounter)
{
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
			EncounterText->SetText(NSLOCTEXT("DeftBlade", "EncounterCleared", "AREA CLEAR - Exit unlocked"));
			break;
	}
}

void UDBPlayerHUDWidget::ShowDeath()
{
	if (bShowingDeath)
	{
		return;
	}
	bShowingDeath = true;
	DeathPanel->SetVisibility(ESlateVisibility::Visible);
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->FlushPressedKeys();
		PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(RetryButton->TakeWidget()));
		PC->bShowMouseCursor = true;
		RetryButton->SetUserFocus(PC);
	}
}

void UDBPlayerHUDWidget::ReleaseDeathInput()
{
	if (!bShowingDeath)
	{
		return;
	}
	bShowingDeath = false;
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		PC->FlushPressedKeys();
	}
}

void UDBPlayerHUDWidget::Retry()
{
	if (bRetryRequested || !HealthSource.IsValid() || !HealthSource->IsDead())
	{
		return;
	}
	bRetryRequested = true;
	RetryButton->SetIsEnabled(false);
	ReleaseDeathInput();
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
	FVector2D Position;
	if (!bShowingDeath && TargetingSource.IsValid() && TargetingSource->IsLockedOn()
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
