#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DBSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class USlider;
class UTextBlock;

UCLASS(Abstract)
class DEFTBLADE_API UDBSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	FSimpleDelegate OnBackRequested;
	void RefreshValues();
	void Save();
	void FocusFirstControl();
	void SetUsingGamepad(bool bGamepad);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> VolumeSlider;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> MouseSlider;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> GamepadSlider;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> VolumeValue;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MouseValue;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GamepadValue;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> InvertMouse;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> InvertGamepad;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ResetButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> BackButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> KeyboardHint;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> GamepadHint;

private:
	void UpdateValueLabels();
	UFUNCTION()
	void VolumeChanged(float Value);
	UFUNCTION()
	void MouseChanged(float Value);
	UFUNCTION()
	void GamepadChanged(float Value);
	UFUNCTION()
	void MouseInversionChanged(bool bChecked);
	UFUNCTION()
	void GamepadInversionChanged(bool bChecked);
	UFUNCTION()
	void ResetDefaults();
	UFUNCTION()
	void GoBack();
};
