#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DBPlayerCharacter.generated.h"

class UAnimMontage;
class UCameraComponent;
class UDBCombatComponent;
class UDBCombatScoreComponent;
class UDBHealthComponent;
class UDBTargetingComponent;
class UDBWeaponData;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

struct FDBTutorialBindings
{
	FKey LightAttack;
	FKey HeavyAttack;
	FKey Jump;
	FKey Evade;
	FKey SwitchWeapon;
	FKey LockOn;
};

/** Input/movement policy lives in C++; the Blueprint child supplies assets. */
UCLASS()
class DEFTBLADE_API ADBPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADBPlayerCharacter();
	/** Release held input/actions for menus and focus loss without restoring airborne budgets. */
	void ResetGameplayInput();
	FDBTutorialBindings GetTutorialBindings(bool bGamepad) const;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
	    AActor* DamageCauser) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual bool CanJumpWhileFalling() const override;
	virtual bool CanJumpInternal_Implementation() const override;
	virtual void OnJumped_Implementation() override;
	virtual void Landed(const FHitResult& Hit) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDBCombatComponent> CombatComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDBCombatScoreComponent> ScoreComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDBHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UDBTargetingComponent> TargetingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Weapon")
	TArray<TObjectPtr<UDBWeaponData>> WeaponLoadout;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> DoubleJumpMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> BackwardDoubleJumpMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> HitReactionMontage;

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

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> EvadeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LightAttackAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> HeavyAttackAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LockOnAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SwitchTargetAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SwitchWeaponAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0"))
	float RunSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0"))
	float WalkSpeed = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0"))
	float SprintSpeed = 900.0f;

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
	void AssistAttackFacing();
	FVector GetAttackInputDirection() const;
	void HandleDeath();
	void Move(const FInputActionValue& Value);
	void LightAttack();
	void HeavyAttack();
	void SwitchWeapon();
	void PauseGame();
	void MoveGamepad(const FInputActionValue& Value);
	void StopGamepadMovement();
	void AddDirectionalMovement(const FVector2D& Axes);
	void ToggleLockOn();
	void SwitchTarget(const FInputActionValue& Value);
	void StopSwitchTarget();
	void StopLookGamepad();
	void LookMouse(const FInputActionValue& Value);
	void LookGamepad(const FInputActionValue& Value);
	void StartWalking();
	void StopWalking();
	void UpdateSprint();
	void StopSprinting();
	void RefreshMovementSpeed();
	void ResetMovementInput();
	void StopJumpMontages();
	void InitializeCameraRotation();
	void AddMappingContext();
	void ReleaseMappingContext();

#if !UE_BUILD_SHIPPING
	void ToggleDebugInvulnerability(FKey, FInputActionValue);
	void DefeatDebugEnemies(FKey, FInputActionValue);
	bool bDebugInvulnerable = false;
#endif

	bool bInitialCameraRotationApplied = false;
	bool bWalkInputHeld = false;
	bool bGamepadRunSelected = false;
	bool bUsingGamepadMovement = false;
	bool bSprinting = false;
	float SwitchInputDirection = 0;
};
