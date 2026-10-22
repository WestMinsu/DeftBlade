#include "Combat/DBCombatComponent.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBCombat, Log, All);

UDBCombatComponent::UDBCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDBCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &ThisClass::ResetCombat);
}

bool UDBCombatComponent::CanProcessInput() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	AController* Controller = Character ? Character->GetController() : nullptr;
	if (!Controller || GetWorld()->IsPaused() || Controller->IsMoveInputIgnored())
	{
		return false;
	}
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
		UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
		if (ViewportClient && (ViewportClient->IgnoreInput()
			|| (ViewportClient->Viewport && !ViewportClient->Viewport->HasFocus())))
		{
			return false;
		}
	}
	return true;
}

void UDBCombatComponent::RequestEvade()
{
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}
	BufferedEvade = {++NextRequestId, GetWorld()->GetTimeSeconds()};
	UE_LOG(LogDBCombat, Verbose, TEXT("%s queued #%llu at %.3f"), *GetOwner()->GetName(),
		BufferedEvade.Id, BufferedEvade.ReceivedAt);
}

void UDBCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!CanProcessInput())
	{
		ResetCombat();
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (BufferedEvade.IsExpired(Now, InputBufferLifetime))
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s expired #%llu at %.3f"), *GetOwner()->GetName(), BufferedEvade.Id, Now);
		BufferedEvade = {};
	}
	if (ActionState == EDBActionState::Evade && Now >= ActionEndsAt)
	{
		FinishAction(ActiveActionId);
	}
	if (ActionState == EDBActionState::None && BufferedEvade.Id != 0)
	{
		TryStartEvade(Now);
	}
}

void UDBCombatComponent::TryStartEvade(double Now)
{
	if (!FMath::IsFinite(EvadeDuration) || EvadeDuration <= 0.0)
	{
		return;
	}

	ActiveActionId = BufferedEvade.Id;
	ActionEndsAt = Now + EvadeDuration;
	ActionState = EDBActionState::Evade;
	BufferedEvade = {};
	UE_LOG(LogDBCombat, Log, TEXT("%s started Evade #%llu at %.3f, ends %.3f"),
		*GetOwner()->GetName(), ActiveActionId, Now, ActionEndsAt);
}

bool UDBCombatComponent::FinishAction(uint64 ExpectedActionId)
{
	if (ActionState == EDBActionState::None || ExpectedActionId != ActiveActionId)
	{
		return false;
	}
	UE_LOG(LogDBCombat, Log, TEXT("%s finished Evade #%llu at %.3f"),
		*GetOwner()->GetName(), ActiveActionId, GetWorld()->GetTimeSeconds());
	ActionState = EDBActionState::None;
	ActiveActionId = 0;
	ActionEndsAt = 0.0;
	return true;
}

void UDBCombatComponent::ResetCombat()
{
	if (ActiveActionId != 0 || BufferedEvade.Id != 0)
	{
		UE_LOG(LogDBCombat, Verbose, TEXT("%s reset active #%llu / pending #%llu"),
			*GetOwner()->GetName(), ActiveActionId, BufferedEvade.Id);
	}
	ActionState = EDBActionState::None;
	ActiveActionId = 0;
	ActionEndsAt = 0.0;
	BufferedEvade = {};
}

void UDBCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	ResetCombat();
	Super::EndPlay(EndPlayReason);
}
