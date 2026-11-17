#include "Game/DBGameUserSettings.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	float ClampSensitivity(float Value)
	{
		return FMath::IsFinite(Value)
		           ? FMath::Clamp(Value, UDBGameUserSettings::MinSensitivity, UDBGameUserSettings::MaxSensitivity)
		           : 1.0f;
	}
}

UDBGameUserSettings* UDBGameUserSettings::Get()
{
	return GEngine ? Cast<UDBGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UDBGameUserSettings::LoadSettings(bool bForceReload)
{
	ResetControlsAndAudio(nullptr);
	Super::LoadSettings(bForceReload);
	MasterVolume = FMath::IsFinite(MasterVolume) ? FMath::Clamp(MasterVolume, 0.0f, 1.0f) : 1.0f;
	MouseSensitivity = ClampSensitivity(MouseSensitivity);
	GamepadSensitivity = ClampSensitivity(GamepadSensitivity);
}

void UDBGameUserSettings::ApplyAudio(UWorld* World) const
{
	if (World && World->IsGameWorld())
	{
		if (FAudioDeviceHandle Audio = World->GetAudioDevice())
		{
			Audio->SetTransientPrimaryVolume(MasterVolume);
		}
	}
}

void UDBGameUserSettings::SetMasterVolume(float Value, UWorld* World)
{
	MasterVolume = FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : 1.0f;
	ApplyAudio(World);
}

void UDBGameUserSettings::SetMouseSensitivity(float Value)
{
	MouseSensitivity = ClampSensitivity(Value);
}

void UDBGameUserSettings::SetGamepadSensitivity(float Value)
{
	GamepadSensitivity = ClampSensitivity(Value);
}

FVector2D UDBGameUserSettings::GetMouseLookScale() const
{
	return FVector2D(MouseSensitivity, bInvertMouseY ? -MouseSensitivity : MouseSensitivity);
}

FVector2D UDBGameUserSettings::GetGamepadLookScale() const
{
	return FVector2D(GamepadSensitivity, bInvertGamepadY ? -GamepadSensitivity : GamepadSensitivity);
}

void UDBGameUserSettings::ResetControlsAndAudio(UWorld* World)
{
	MasterVolume = MouseSensitivity = GamepadSensitivity = 1.0f;
	bInvertMouseY = bInvertGamepadY = false;
	ApplyAudio(World);
}
