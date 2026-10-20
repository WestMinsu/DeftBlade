#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DBPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

/** Input/movement policy lives in C++; the Blueprint child supplies assets. */
UCLASS()
class DEFTBLADE_API ADBPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADBPlayerCharacter();
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> GameplayMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> GamepadMoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> GamepadLookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> WalkAction;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0"))
	float RunSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0"))
	float WalkSpeed = 200.0f;

	/** Stick magnitude after the IMC deadzone; separate thresholds prevent gait flicker. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float GamepadRunThreshold = 0.70f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float GamepadWalkThreshold = 0.60f;

	/** Degrees per mouse input unit; mouse deltas already represent displacement. */
	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (ClampMin = "0"))
	FVector2D MouseSensitivity = FVector2D(0.12, 0.12);

	/** Degrees per second at full stick deflection. */
	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (ClampMin = "0"))
	FVector2D GamepadLookRate = FVector2D(180.0, 120.0);

private:
	void Move(const FInputActionValue& Value);
	void MoveGamepad(const FInputActionValue& Value);
	void StopGamepadMovement();
	void AddCameraRelativeMovement(const FVector2D& Axes);
	void LookMouse(const FInputActionValue& Value);
	void LookGamepad(const FInputActionValue& Value);
	void StartWalking();
	void StopWalking();
	void ResetMovementInput();
	void InitializeCameraRotation();
	void AddMappingContext();
	void ReleaseMappingContext();

	bool bInitialCameraRotationApplied = false;
	bool bWalkInputHeld = false;
	bool bGamepadRunSelected = false;
	bool bUsingGamepadMovement = false;
};
