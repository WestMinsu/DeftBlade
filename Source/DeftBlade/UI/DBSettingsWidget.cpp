#include "UI/DBSettingsWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Game/DBGameUserSettings.h"

void UDBSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	VolumeSlider->SetStepSize(0.05f);
	for (USlider* Slider : {MouseSlider.Get(), GamepadSlider.Get()})
	{
		Slider->SetMinValue(UDBGameUserSettings::MinSensitivity);
		Slider->SetMaxValue(UDBGameUserSettings::MaxSensitivity);
		Slider->SetStepSize(0.05f);
	}
	VolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::VolumeChanged);
	MouseSlider->OnValueChanged.AddDynamic(this, &ThisClass::MouseChanged);
	GamepadSlider->OnValueChanged.AddDynamic(this, &ThisClass::GamepadChanged);
	InvertMouse->OnCheckStateChanged.AddDynamic(this, &ThisClass::MouseInversionChanged);
	InvertGamepad->OnCheckStateChanged.AddDynamic(this, &ThisClass::GamepadInversionChanged);
	ResetButton->OnClicked.AddDynamic(this, &ThisClass::ResetDefaults);
	BackButton->OnClicked.AddDynamic(this, &ThisClass::GoBack);
	SetUsingGamepad(false);
}

void UDBSettingsWidget::RefreshValues()
{
	if (const auto* Settings = UDBGameUserSettings::Get())
	{
		VolumeSlider->SetValue(Settings->GetMasterVolume());
		MouseSlider->SetValue(Settings->GetMouseSensitivity());
		GamepadSlider->SetValue(Settings->GetGamepadSensitivity());
		InvertMouse->SetIsChecked(Settings->IsMouseYInverted());
		InvertGamepad->SetIsChecked(Settings->IsGamepadYInverted());
		UpdateValueLabels();
	}
}

void UDBSettingsWidget::UpdateValueLabels()
{
	FNumberFormattingOptions Format;
	Format.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
	VolumeValue->SetText(FText::Format(NSLOCTEXT("DeftBlade", "VolumePercent", "{0}%"),
	    FText::AsNumber(FMath::RoundToInt(VolumeSlider->GetValue() * 100.0f))));
	MouseValue->SetText(FText::Format(
	    NSLOCTEXT("DeftBlade", "SensitivityScale", "{0}x"), FText::AsNumber(MouseSlider->GetValue(), &Format)));
	GamepadValue->SetText(FText::Format(
	    NSLOCTEXT("DeftBlade", "SensitivityScale", "{0}x"), FText::AsNumber(GamepadSlider->GetValue(), &Format)));
}

void UDBSettingsWidget::VolumeChanged(float Value)
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->SetMasterVolume(Value, GetWorld());
		UpdateValueLabels();
	}
}

void UDBSettingsWidget::MouseChanged(float Value)
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->SetMouseSensitivity(Value);
		UpdateValueLabels();
	}
}

void UDBSettingsWidget::GamepadChanged(float Value)
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->SetGamepadSensitivity(Value);
		UpdateValueLabels();
	}
}

void UDBSettingsWidget::MouseInversionChanged(bool bChecked)
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->SetInvertMouseY(bChecked);
	}
}

void UDBSettingsWidget::GamepadInversionChanged(bool bChecked)
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->SetInvertGamepadY(bChecked);
	}
}

void UDBSettingsWidget::ResetDefaults()
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->ResetControlsAndAudio(GetWorld());
		RefreshValues();
	}
}

void UDBSettingsWidget::Save()
{
	if (auto* Settings = UDBGameUserSettings::Get())
	{
		Settings->SaveSettings();
	}
}

void UDBSettingsWidget::GoBack()
{
	OnBackRequested.ExecuteIfBound();
}

void UDBSettingsWidget::FocusFirstControl()
{
	VolumeSlider->SetUserFocus(GetOwningPlayer());
}

void UDBSettingsWidget::SetUsingGamepad(bool bGamepad)
{
	KeyboardHint->SetVisibility(bGamepad ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	GamepadHint->SetVisibility(bGamepad ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UDBSettingsWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	for (UButton* Button : {ResetButton.Get(), BackButton.Get()})
	{
		Button->SetBackgroundColor(Button->HasUserFocus(GetOwningPlayer()) || Button->IsHovered()
		                               ? FLinearColor(0.33f, 0.035f, 0.055f)
		                               : FLinearColor(0.025f, 0.03f, 0.037f));
	}
}
