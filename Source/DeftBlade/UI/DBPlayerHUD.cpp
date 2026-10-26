#include "UI/DBPlayerHUD.h"

#include "Combat/DBHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "UI/DBPlayerHUDWidget.h"

void ADBPlayerHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController() || !WidgetClass) { return; }
	SetActorTickEnabled(false);
	PlayerWidget = CreateWidget<UDBPlayerHUDWidget>(PC, WidgetClass);
	if (!PlayerWidget) { return; }
	PlayerWidget->AddToPlayerScreen();
	PC->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::PawnChanged);
	PawnChanged(nullptr, PC->GetPawn());
}

void ADBPlayerHUD::PawnChanged(APawn*, APawn* NewPawn)
{
	if (PlayerWidget) { PlayerWidget->BindHealth(NewPawn ? NewPawn->FindComponentByClass<UDBHealthComponent>() : nullptr); }
}

void ADBPlayerHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (APlayerController* PC = GetOwningPlayerController()) { PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::PawnChanged); }
	if (PlayerWidget)
	{
		PlayerWidget->BindHealth(nullptr);
		PlayerWidget->RemoveFromParent();
		PlayerWidget = nullptr;
	}
	Super::EndPlay(Reason);
}
