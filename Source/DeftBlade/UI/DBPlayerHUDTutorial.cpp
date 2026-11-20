#include "UI/DBPlayerHUDWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBTrainingDummy.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Game/DBCombatEncounter.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"

namespace
{
	FText PromptKey(const FKey& Key)
	{
		if (Key == EKeys::LeftMouseButton)
		{
			return NSLOCTEXT("DeftBlade", "TutorialLeftMouse", "LMB");
		}
		if (Key == EKeys::RightMouseButton)
		{
			return NSLOCTEXT("DeftBlade", "TutorialRightMouse", "RMB");
		}
		if (Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			return FText::FromString(TEXT("A"));
		}
		if (Key == EKeys::Gamepad_FaceButton_Left)
		{
			return FText::FromString(TEXT("X"));
		}
		if (Key == EKeys::Gamepad_FaceButton_Top)
		{
			return FText::FromString(TEXT("Y"));
		}
		if (Key == EKeys::Gamepad_RightTrigger || Key == EKeys::Gamepad_RightTriggerAxis)
		{
			return NSLOCTEXT("DeftBlade", "TutorialRightTrigger", "RT");
		}
		return Key.GetDisplayName(false);
	}
}

void UDBPlayerHUDWidget::UpdateTutorial()
{
	if (!TutorialCard || !TutorialShade || !TutorialTitle || !TutorialBody || !TutorialInput)
	{
		return;
	}
	ETutorialHint Next = ETutorialHint::None;
	if (Screen == EScreen::Tutorial)
	{
		const bool bReady = FPlatformTime::Seconds() >= ParryInputAllowedAt;
		if (bReady != bParryInputReady)
		{
			bParryInputReady = bReady;
			RenderTutorial();
		}
		Next = ETutorialHint::Parry;
	}
	else if (Screen == EScreen::Gameplay && HealthSource.IsValid() && !HealthSource->IsDead()
	         && EncounterSource.IsValid())
	{
		const ADBTrainingDummy* Dummy = EncounterSource->GetTrainingDummy();
		if (IsValid(Dummy))
		{
			const double StageTime = GetWorld()->GetTimeSeconds() - TutorialStageStartedAt;
			switch (EncounterSource->GetState())
			{
				case EDBEncounterState::Ready:
				{
					const FVector Position = HealthSource->GetOwner()->GetActorLocation();
					if (FVector::DistSquared(Dummy->GetActorLocation(), Position) <= FMath::Square(340.0))
					{
						Next = Dummy->HasPracticeHit() ? ETutorialHint::PracticeCombos : ETutorialHint::Practice;
					}
					else
					{
						switch (EncounterSource->GetTraversalLesson(Position))
						{
							case EDBTraversalLesson::Jump:
								Next = ETutorialHint::Jump;
								break;
							case EDBTraversalLesson::DoubleJump:
								Next = ETutorialHint::DoubleJump;
								break;
							case EDBTraversalLesson::EvadeSprint:
								Next = ETutorialHint::EvadeSprint;
								break;
							case EDBTraversalLesson::None:
								Next = bReachedContextLesson ? ETutorialHint::None : ETutorialHint::Movement;
								break;
						}
					}
					bReachedContextLesson |= Next != ETutorialHint::None && Next != ETutorialHint::Movement;
					break;
				}
				case EDBEncounterState::Active:
					Next = StageTime < 7.0 ? ETutorialHint::MixedCombo : ETutorialHint::None;
					break;
				case EDBEncounterState::AirReinforcement:
					Next = StageTime < 12.0 ? ETutorialHint::AirCombo : ETutorialHint::None;
					break;
				default:
					break;
			}
		}
	}
	if (Next != TutorialHint)
	{
		TutorialHint = Next;
		RenderTutorial();
	}
}

void UDBPlayerHUDWidget::RenderTutorial()
{
	if (!TutorialCard || !TutorialShade || !TutorialTitle || !TutorialBody || !TutorialInput)
	{
		return;
	}
	const auto* Player = Cast<ADBPlayerCharacter>(GetOwningPlayerPawn());
	const bool bVisible = Player && TutorialHint != ETutorialHint::None;
	const bool bModal = bVisible && TutorialHint == ETutorialHint::Parry;
	TutorialShade->SetVisibility(bModal ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	TutorialCard->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bVisible)
	{
		return;
	}
	if (auto* CardSlot = Cast<UCanvasPanelSlot>(TutorialCard->Slot))
	{
		CardSlot->SetAnchors(FAnchors(bModal ? 0.5f : 0.0f, 0.94f));
		CardSlot->SetAlignment(FVector2D(bModal ? 0.5f : 0.0f, 1.0f));
		CardSlot->SetPosition(FVector2D(bModal ? 0.0f : 44.0f, 0.0f));
		CardSlot->SetSize(bModal ? FVector2D(760, 194) : FVector2D(640, 150));
	}
	const FDBTutorialBindings Bindings = Player->GetTutorialBindings(bUsingGamepad);
	const FText Light = PromptKey(Bindings.LightAttack);
	const FText Heavy = PromptKey(Bindings.HeavyAttack);
	FText Title;
	FText Body;
	FText Input;
	switch (TutorialHint)
	{
		case ETutorialHint::Movement:
			Title = NSLOCTEXT("DeftBlade", "TutorialMovementTitle", "MOVEMENT");
			Body = NSLOCTEXT("DeftBlade", "TutorialMovementBody", "Follow the passage ahead.");
			Input = bUsingGamepad ? NSLOCTEXT("DeftBlade", "TutorialStick", "LEFT STICK   Move")
			                      : NSLOCTEXT("DeftBlade", "TutorialWASD", "W A S D   Move");
			break;
		case ETutorialHint::Jump:
			Title = NSLOCTEXT("DeftBlade", "TutorialJumpTitle", "JUMP");
			Body = NSLOCTEXT("DeftBlade", "TutorialJumpBody", "Move forward and jump onto the low step.");
			Input = FText::Format(NSLOCTEXT("DeftBlade", "TutorialJumpInput", "{0}   Jump"), PromptKey(Bindings.Jump));
			break;
		case ETutorialHint::DoubleJump:
			Title = NSLOCTEXT("DeftBlade", "TutorialDoubleJumpTitle", "DOUBLE JUMP");
			Body =
			    NSLOCTEXT("DeftBlade", "TutorialDoubleJumpBody", "Jump again in the air to reach the higher platform.");
			Input =
			    FText::Format(NSLOCTEXT("DeftBlade", "TutorialDoubleJumpInput", "{0}   Jump   →   {0}   Jump again"),
			        PromptKey(Bindings.Jump));
			break;
		case ETutorialHint::EvadeSprint:
			Title = NSLOCTEXT("DeftBlade", "TutorialEvadeSprintTitle", "DODGE AND SPRINT");
			Body = NSLOCTEXT(
			    "DeftBlade", "TutorialEvadeSprintBody", "Move and hold the dodge button to sprint out of the dash.");
			Input = FText::Format(NSLOCTEXT("DeftBlade", "TutorialEvadeSprintInput",
			                          "Tap {0}   Dodge     ·     Hold {0} while moving   Sprint"),
			    PromptKey(Bindings.Evade));
			break;
		case ETutorialHint::Practice:
		case ETutorialHint::PracticeCombos:
			Title = NSLOCTEXT("DeftBlade", "TutorialPracticeTitle", "PRACTICE TARGET");
			Body =
			    TutorialHint == ETutorialHint::PracticeCombos
			        ? NSLOCTEXT("DeftBlade", "TutorialSameAttackCombo", "Repeat either attack to chain its own combo.")
			        : NSLOCTEXT("DeftBlade", "TutorialPracticeBody", "Step in and try both attacks on the target.");
			Input = FText::Format(
			    NSLOCTEXT("DeftBlade", "TutorialBasicAttacks", "{0}   Light attack       {1}   Heavy attack"), Light,
			    Heavy);
			break;
		case ETutorialHint::MixedCombo:
			Title = NSLOCTEXT("DeftBlade", "TutorialMixedTitle", "MIX YOUR COMBOS");
			Body = NSLOCTEXT("DeftBlade", "TutorialMixedBody",
			    "Follow a light attack with a heavy attack to branch into a different combo.");
			Input = FText::Format(
			    NSLOCTEXT("DeftBlade", "TutorialMixedInput", "{0}   Light   →   {1}   Heavy"), Light, Heavy);
			break;
		case ETutorialHint::AirCombo:
			Title = NSLOCTEXT("DeftBlade", "TutorialAirTitle", "AIR COMBO");
			Body = NSLOCTEXT("DeftBlade", "TutorialAirBody",
			    "Launch the enemy after it lands. Follow it up and finish with a slam.");
			Input = FText::Format(NSLOCTEXT("DeftBlade", "TutorialAirInput",
			                          "{0} + {1}   Launch   →   {2} ×3   Air combo   →   {1}   Slam"),
			    PromptKey(Bindings.Jump), Heavy, Light);
			break;
		case ETutorialHint::Parry:
			Title = NSLOCTEXT("DeftBlade", "TutorialParryTitle", "PARRY THE STRIKE");
			Body = FText::Format(
			    NSLOCTEXT("DeftBlade", "TutorialParryBody",
			        "A glowing weapon means your hits will not interrupt the attack. Dodge with {0}, or time a light attack to parry."),
			    PromptKey(Bindings.Evade));
			Input =
			    bParryInputReady
			        ? FText::Format(
			              NSLOCTEXT("DeftBlade", "TutorialParryInput", "Press {0}   Parry and continue"), Light)
			        : FText::Format(NSLOCTEXT("DeftBlade", "TutorialParryWait", "Get ready   ·   {0} to parry"), Light);
			break;
		default:
			break;
	}
	TutorialTitle->SetText(Title);
	TutorialBody->SetText(Body);
	TutorialInput->SetText(Input);
	TutorialInput->SetRenderOpacity(!bModal || bParryInputReady ? 1.0f : 0.55f);
}

bool UDBPlayerHUDWidget::ShowParryLesson(UDBCombatComponent* Attacker, const FVector& ImpactPoint)
{
	auto* PC = GetOwningPlayer();
	auto* Player = Cast<ADBPlayerCharacter>(GetOwningPlayerPawn());
	if (bParryTaught || Screen != EScreen::Gameplay || !PC || !Player || !IsValid(Attacker) || !TutorialCard
	    || !TutorialShade || !TutorialTitle || !TutorialBody || !TutorialInput
	    || (!Player->GetTutorialBindings(false).LightAttack.IsValid()
	        && !Player->GetTutorialBindings(true).LightAttack.IsValid())
	    || !PC->SetPause(true))
	{
		return false;
	}
	Player->ResetGameplayInput();
	TutorialAttacker = Attacker;
	TutorialImpactPoint = ImpactPoint;
	TutorialAttackId = Attacker->GetActiveActionId();
	ParryInputAllowedAt = FPlatformTime::Seconds() + 1.5;
	bParryInputReady = false;
	Screen = EScreen::Tutorial;
	PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(TakeWidget()));
	SetUserFocus(PC);
	UpdateTutorial();
	return true;
}

bool UDBPlayerHUDWidget::HandleTutorialInput(const FKey& Key, bool bRepeat)
{
	const auto* Player = Cast<ADBPlayerCharacter>(GetOwningPlayerPawn());
	if (Screen != EScreen::Tutorial || !Player || Key != Player->GetTutorialBindings(Key.IsGamepadKey()).LightAttack)
	{
		return false;
	}
	if (bRepeat || FPlatformTime::Seconds() < ParryInputAllowedAt)
	{
		return true;
	}
	TWeakObjectPtr<UDBCombatComponent> Attacker = TutorialAttacker;
	const uint64 AttackId = TutorialAttackId;
	const FVector ImpactPoint = TutorialImpactPoint;
	ReleaseMenuInput();
	UWidgetBlueprintLibrary::SetFocusToGameViewport();
	if (Attacker.IsValid())
	{
		bParryTaught = Player->FindComponentByClass<UDBCombatComponent>()->ParryTutorialAttack(
		    *Attacker.Get(), AttackId, ImpactPoint);
	}
	return true;
}
