#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "DBGameUserSettings.generated.h"

UCLASS(Config = GameUserSettings)
class DEFTBLADE_API UDBGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static UDBGameUserSettings* Get();
	static constexpr float MinSensitivity = 0.25f;
	static constexpr float MaxSensitivity = 3.0f;

	virtual void LoadSettings(bool bForceReload = false) override;
	void ApplyAudio(UWorld* World) const;
	void ResetControlsAndAudio(UWorld* World);
	void SetMasterVolume(float Value, UWorld* World);
	void SetMouseSensitivity(float Value);
	void SetGamepadSensitivity(float Value);
	void SetInvertMouseY(bool bInvert)
	{
		bInvertMouseY = bInvert;
	}
	void SetInvertGamepadY(bool bInvert)
	{
		bInvertGamepadY = bInvert;
	}

	float GetMasterVolume() const
	{
		return MasterVolume;
	}
	float GetMouseSensitivity() const
	{
		return MouseSensitivity;
	}
	float GetGamepadSensitivity() const
	{
		return GamepadSensitivity;
	}
	bool IsMouseYInverted() const
	{
		return bInvertMouseY;
	}
	bool IsGamepadYInverted() const
	{
		return bInvertGamepadY;
	}
	FVector2D GetMouseLookScale() const;
	FVector2D GetGamepadLookScale() const;

private:
	UPROPERTY(Config)
	float MasterVolume = 1.0f;
	UPROPERTY(Config)
	float MouseSensitivity = 1.0f;
	UPROPERTY(Config)
	float GamepadSensitivity = 1.0f;
	UPROPERTY(Config)
	bool bInvertMouseY = false;
	UPROPERTY(Config)
	bool bInvertGamepadY = false;
};
