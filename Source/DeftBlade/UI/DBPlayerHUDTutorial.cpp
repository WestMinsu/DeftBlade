#include "UI/DBPlayerHUDWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBTargetingComponent.h"
#include "Combat/DBTrainingDummy.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/DBCombatEncounter.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr double PracticeComboDuration = 3.5;
	constexpr double CombatLockOnDuration = 5.0;
	constexpr double LockOnConfirmDuration = 0.8;
	constexpr TCHAR GamepadAToken[] = TEXT("\u24B6");

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
		if (Key == EKeys::MiddleMouseButton)
		{
			return NSLOCTEXT("DeftBlade", "TutorialMiddleMouse", "MOUSE WHEEL CLICK");
		}
		if (Key == EKeys::Gamepad_RightThumbstick)
		{
			return NSLOCTEXT("DeftBlade", "TutorialRightStickClick", "RIGHT STICK CLICK (R3)");
		}
		if (Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			return FText::FromString(GamepadAToken);
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
		if (Key == EKeys::Gamepad_DPad_Up)
		{
			return NSLOCTEXT("DeftBlade", "TutorialDPadUp", "D-PAD ↑");
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
		const EDBEncounterState State = EncounterSource->GetState();
		if (State == EDBEncounterState::BossIntroduction && EncounterSource->GetBossCheckpoint())
		{
			Next = ETutorialHint::Checkpoint;
		}
		else if (IsValid(Dummy))
		{
			const double Now = GetWorld()->GetTimeSeconds();
			const double StageTime = Now - TutorialStageStartedAt;
			if (LockOnLearnedAt < 0.0 && (State == EDBEncounterState::Ready || State == EDBEncounterState::Active)
			    && TargetingSource.IsValid() && TargetingSource->IsLockedOn())
			{
				LockOnLearnedAt = Now;
			}
			switch (State)
			{
				case EDBEncounterState::Ready:
				{
					const FVector Position = HealthSource->GetOwner()->GetActorLocation();
					if (FVector::DistSquared(Dummy->GetActorLocation(), Position) <= FMath::Square(340.0))
					{
						if (!Dummy->HasPracticeHit())
						{
							Next = ETutorialHint::Practice;
						}
						else
						{
							if (PracticeCombosShownAt < 0.0)
							{
								PracticeCombosShownAt = Now;
							}
							if (Now - PracticeCombosShownAt < PracticeComboDuration)
							{
								Next = ETutorialHint::PracticeCombos;
							}
							else if (LockOnLearnedAt < 0.0
							         || (TutorialHint == ETutorialHint::LockOn
							             && Now - LockOnLearnedAt < LockOnConfirmDuration))
							{
								Next = ETutorialHint::LockOn;
							}
							else if (!bChargedAttackTried)
							{
								Next = ETutorialHint::ChargedHeavy;
							}
						}
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
				{
					double LockOnEnd = TutorialStageStartedAt;
					if (LockOnLearnedAt < 0.0)
					{
						LockOnEnd += CombatLockOnDuration;
					}
					else if (LockOnLearnedAt >= TutorialStageStartedAt)
					{
						LockOnEnd = FMath::Min(
						    TutorialStageStartedAt + CombatLockOnDuration, LockOnLearnedAt + LockOnConfirmDuration);
					}
					if (Now < LockOnEnd)
					{
						Next = ETutorialHint::LockOn;
					}
					else if (Now - LockOnEnd < 7.0)
					{
						Next = ETutorialHint::MixedCombo;
					}
					break;
				}
				case EDBEncounterState::AirReinforcement:
					Next = StageTime < 12.0 ? ETutorialHint::AirCombo : ETutorialHint::None;
					break;
				case EDBEncounterState::Reinforcements:
					Next = StageTime < 12.0 ? ETutorialHint::WeaponSwitch : ETutorialHint::None;
					break;
				case EDBEncounterState::Cleared:
					if (!bChargedAttackTried && EncounterSource->GetBoss())
					{
						Next = ETutorialHint::ChargedHeavy;
					}
					break;
				default:
					break;
			}
		}
	}
	if (Next == ETutorialHint::ChargedHeavy)
	{
		const auto* Combat = HealthSource->GetOwner()->FindComponentByClass<UDBCombatComponent>();
		if (Combat && Combat->GetAttackId() == EDBAttackId::ChargedHeavy)
		{
			bChargedAttackTried = true;
			Next = ETutorialHint::None;
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
	const bool bCheckpoint = TutorialHint == ETutorialHint::Checkpoint;
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
		CardSlot->SetSize(bModal ? FVector2D(760, 194) : bCheckpoint ? FVector2D(240, 54) : FVector2D(640, 150));
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
		case ETutorialHint::LockOn:
			Title = NSLOCTEXT("DeftBlade", "TutorialLockOnTitle", "LOCK ON");
			Body = NSLOCTEXT("DeftBlade", "TutorialLockOnBody",
			    "Keep the target on screen and lock on to track it. Press again to release.");
			Input = FText::Format(
			    NSLOCTEXT("DeftBlade", "TutorialLockOnInput", "{0}   Lock on / Release"), PromptKey(Bindings.LockOn));
			break;
		case ETutorialHint::ChargedHeavy:
			Title = NSLOCTEXT("DeftBlade", "TutorialChargeTitle", "CHARGE ATTACK");
			Body =
			    NSLOCTEXT("DeftBlade", "TutorialChargeBody", "Release when your weapon glows to rush in and strike.");
			Input = FText::Format(NSLOCTEXT("DeftBlade", "TutorialChargeInput", "Hold {0}   Charge"), Heavy);
			break;
		case ETutorialHint::AirCombo:
			Title = NSLOCTEXT("DeftBlade", "TutorialAirTitle", "AIR COMBO");
			Body = NSLOCTEXT("DeftBlade", "TutorialAirBody",
			    "Launch the enemy, follow up with air attacks, then finish with a slam.");
			Input = FText::Format(NSLOCTEXT("DeftBlade", "TutorialAirInput",
			                          "{0} + {1}   Launch   →   {2} ×3   Air combo   →   {1}   Slam"),
			    PromptKey(Bindings.Jump), Heavy, Light);
			break;
		case ETutorialHint::Checkpoint:
			Title = NSLOCTEXT("DeftBlade", "CheckpointSavedTitle", "CHECKPOINT SAVED");
			break;
		case ETutorialHint::WeaponSwitch:
			Title = NSLOCTEXT("DeftBlade", "TutorialWeaponSwitchTitle", "SWITCH WEAPONS");
			Body = NSLOCTEXT("DeftBlade", "TutorialWeaponSwitchBody",
			    "Switch during the sword's second light attack to sweep nearby enemies with the axe.");
			Input = FText::Format(NSLOCTEXT("DeftBlade", "TutorialWeaponSwitchInput",
			                          "{0}  Switch weapon     ·     {1} ×2 → {0}  Switch combo"),
			    PromptKey(Bindings.SwitchWeapon), Light);
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
	const ESlateVisibility Details = bCheckpoint ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
	TutorialBody->SetVisibility(Details);
	RenderTutorialInput(Input, Details, !bModal || bParryInputReady ? 1.0f : 0.55f);
}

void UDBPlayerHUDWidget::RenderTutorialInput(const FText& Input, ESlateVisibility InputVisibility, float Opacity)
{
	const FString Line = Input.ToString();
	const bool bHasGamepadA = Line.Contains(GamepadAToken);
	const bool bChanged = !TutorialInput->GetText().EqualTo(Input);
	if (bHasGamepadA && !TutorialGamepadInput)
	{
		if (auto* Column = Cast<UVerticalBox>(TutorialInput->GetParent()))
		{
			TutorialGamepadInput = WidgetTree->ConstructWidget<UHorizontalBox>(
			    UHorizontalBox::StaticClass(), TEXT("TutorialGamepadInput"));
			const auto* TextSlot = CastChecked<UVerticalBoxSlot>(TutorialInput->Slot);
			Column->AddChildToVerticalBox(TutorialGamepadInput)->SetPadding(TextSlot->GetPadding());
		}
	}
	const bool bUseButtonIcon = bHasGamepadA && TutorialGamepadInput;
	TutorialInput->SetText(
	    bHasGamepadA && !bUseButtonIcon ? FText::FromString(Line.Replace(GamepadAToken, TEXT("GAMEPAD A"))) : Input);
	TutorialInput->SetVisibility(bUseButtonIcon ? ESlateVisibility::Collapsed : InputVisibility);
	TutorialInput->SetRenderOpacity(Opacity);
	if (!TutorialGamepadInput)
	{
		return;
	}
	TutorialGamepadInput->SetVisibility(bUseButtonIcon ? InputVisibility : ESlateVisibility::Collapsed);
	TutorialGamepadInput->SetRenderOpacity(Opacity);
	if (!bUseButtonIcon || (!bChanged && TutorialGamepadInput->GetChildrenCount() > 0))
	{
		return;
	}

	TutorialGamepadInput->ClearChildren();
	const auto MakeText = [this](const FText& Text, const FSlateFontInfo& Font)
	{
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(Text);
		Label->SetFont(Font);
		Label->SetColorAndOpacity(TutorialInput->GetColorAndOpacity());
		Label->SetShadowOffset(TutorialInput->GetShadowOffset());
		Label->SetShadowColorAndOpacity(TutorialInput->GetShadowColorAndOpacity());
		return Label;
	};
	TArray<FString> Parts;
	Line.ParseIntoArray(Parts, GamepadAToken, false);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Index > 0)
		{
			constexpr float Diameter = 21.0f;
			auto* Badge = WidgetTree->ConstructWidget<USizeBox>();
			Badge->SetWidthOverride(Diameter);
			Badge->SetHeightOverride(Diameter);
			auto* Outline = WidgetTree->ConstructWidget<UBorder>();
			Outline->SetBrush(FSlateRoundedBoxBrush(
			    FLinearColor::Transparent, TutorialInput->GetColorAndOpacity(), 1.5f, FVector2D(Diameter)));
			Outline->SetPadding(FMargin(0.0f));
			Outline->SetHorizontalAlignment(HAlign_Center);
			Outline->SetVerticalAlignment(VAlign_Center);
			FSlateFontInfo Font = TutorialInput->GetFont();
			Font.Size = 13.0f;
			Outline->SetContent(MakeText(FText::FromString(TEXT("A")), Font));
			Badge->SetContent(Outline);
			TutorialGamepadInput->AddChildToHorizontalBox(Badge)->SetVerticalAlignment(VAlign_Center);
		}
		if (!Parts[Index].IsEmpty())
		{
			TutorialGamepadInput
			    ->AddChildToHorizontalBox(MakeText(FText::FromString(Parts[Index]), TutorialInput->GetFont()))
			    ->SetVerticalAlignment(VAlign_Center);
		}
	}
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
