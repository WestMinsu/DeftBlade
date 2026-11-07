#include "UI/DBPlayerHUD.h"

#include "Combat/DBHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "Game/DBCombatEncounter.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/DBPlayerHUDWidget.h"

void ADBPlayerHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController() || !WidgetClass)
	{
		return;
	}
	SetActorTickEnabled(false);
	PlayerWidget = CreateWidget<UDBPlayerHUDWidget>(PC, WidgetClass);
	if (!PlayerWidget)
	{
		return;
	}
	PlayerWidget->AddToPlayerScreen();
	for (TActorIterator<ADBCombatEncounter> It(GetWorld()); It; ++It)
	{
		PlayerWidget->BindEncounter(*It);
		break;
	}
	PC->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::PawnChanged);
	FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
	    this, &ThisClass::ApplicationActivationChanged);
	PawnChanged(nullptr, PC->GetPawn());
}

void ADBPlayerHUD::PauseGame()
{
	if (PlayerWidget)
	{
		PlayerWidget->PauseGame();
	}
}

void ADBPlayerHUD::ApplicationActivationChanged(bool bActive)
{
	if (!bActive)
	{
		PauseGame();
	}
	else if (PlayerWidget)
	{
		PlayerWidget->RequestMenuFocus();
	}
}

void ADBPlayerHUD::PawnChanged(APawn*, APawn* NewPawn)
{
	if (PlayerWidget)
	{
		PlayerWidget->BindHealth(NewPawn ? NewPawn->FindComponentByClass<UDBHealthComponent>() : nullptr);
	}
}

void ADBPlayerHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(this);
	}
	if (APlayerController* PC = GetOwningPlayerController())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::PawnChanged);
	}
	if (PlayerWidget)
	{
		PlayerWidget->BindHealth(nullptr);
		PlayerWidget->RemoveFromParent();
		PlayerWidget = nullptr;
	}
	Super::EndPlay(Reason);
}
