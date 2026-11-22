#include "UI/DBPlayerHUD.h"

#include "Combat/DBHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "Game/DBCombatEncounter.h"
#include "Game/DBGameMode.h"
#include "Game/DBGameUserSettings.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "UI/DBPlayerHUDWidget.h"
#include "UnrealClient.h"

namespace
{
	// Slate still receives device events while a menu's UIOnly mode bypasses PlayerInput.
	class FDBInputDeviceTracker final : public IInputProcessor
	{
	public:
		explicit FDBInputDeviceTracker(UDBPlayerHUDWidget* InWidget) : Widget(InWidget)
		{
		}
		virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override
		{
		}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			auto* HUD = Update(Event.GetUserIndex(), Event.GetKey().IsGamepadKey());
			return HUD && HUD->HandleTutorialInput(Event.GetKey(), Event.IsRepeat());
		}
		virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& Event) override
		{
			if (FMath::Abs(Event.GetAnalogValue()) > 0.2f)
			{
				Update(Event.GetUserIndex(), Event.GetKey().IsGamepadKey());
			}
			return false;
		}
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
		{
			auto* HUD = Update(Event.GetUserIndex(), false);
			return HUD && HUD->HandleTutorialInput(Event.GetEffectingButton(), false);
		}
		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication&, const FPointerEvent& Event) override
		{
			auto* HUD = Update(Event.GetUserIndex(), false);
			return HUD && HUD->HandleTutorialInput(Event.GetEffectingButton(), false);
		}
		virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& Event) override
		{
			if (!Event.IsTouchEvent() && Event.GetCursorDelta().SizeSquared() > 1.f)
			{
				Update(Event.GetUserIndex(), false);
			}
			return false;
		}

	private:
		UDBPlayerHUDWidget* Update(int32 UserIndex, bool bGamepad)
		{
			UDBPlayerHUDWidget* HUD = Widget.Get();
			ULocalPlayer* Player = HUD ? HUD->GetOwningLocalPlayer() : nullptr;
			if (!Player || !HUD->IsInViewport() || !Player->GetSlateUser().IsValid()
			    || Player->GetSlateUser()->GetUserIndex() != UserIndex)
			{
				return nullptr;
			}
			const bool bViewportFocused = Player->ViewportClient && Player->ViewportClient->Viewport
			                              && Player->ViewportClient->Viewport->HasFocus();
			if (bViewportFocused || HUD->HasUserFocus(HUD->GetOwningPlayer())
			    || HUD->HasUserFocusedDescendants(HUD->GetOwningPlayer()))
			{
				HUD->SetUsingGamepad(bGamepad);
				return HUD;
			}
			return nullptr;
		}
		TWeakObjectPtr<UDBPlayerHUDWidget> Widget;
	};
}

void ADBPlayerHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController() || !WidgetClass)
	{
		return;
	}
	SetActorTickEnabled(false);
	if (const auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->ApplyAudio(GetWorld());
	}
	PlayerWidget = CreateWidget<UDBPlayerHUDWidget>(PC, WidgetClass);
	if (!PlayerWidget)
	{
		return;
	}
	PlayerWidget->AddToPlayerScreen();
	InputDeviceTracker = MakeShared<FDBInputDeviceTracker>(PlayerWidget);
	FSlateApplication::Get().RegisterInputPreProcessor(InputDeviceTracker);
	for (TActorIterator<ADBCombatEncounter> It(GetWorld()); It; ++It)
	{
		PlayerWidget->BindEncounter(*It);
		break;
	}
	PC->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::PawnChanged);
	FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
	    this, &ThisClass::ApplicationActivationChanged);
	PawnChanged(nullptr, PC->GetPawn());
	if (ADBGameMode::IsTitleMap(this))
	{
		PlayerWidget->ShowTitle();
	}
}

void ADBPlayerHUD::PauseGame()
{
	if (PlayerWidget)
	{
		PlayerWidget->PauseGame();
	}
}

bool ADBPlayerHUD::ShowParryLesson(UDBCombatComponent* Attacker, const FVector& ImpactPoint)
{
	return PlayerWidget && PlayerWidget->ShowParryLesson(Attacker, ImpactPoint);
}

void ADBPlayerHUD::ApplicationActivationChanged(bool bActive)
{
	if (!bActive)
	{
		PauseGame();
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
		FSlateApplication::Get().UnregisterInputPreProcessor(InputDeviceTracker);
	}
	InputDeviceTracker.Reset();
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
