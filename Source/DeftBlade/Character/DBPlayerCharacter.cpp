#include "Character/DBPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/DBCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBPlayer, Log, All);

ADBPlayerCharacter::ADBPlayerCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0, 720.0, 0.0);
	Movement->MaxWalkSpeed = RunSpeed;
	Movement->MinAnalogWalkSpeed = 0.0f;
	Movement->MaxAcceleration = 2048.0f;
	Movement->BrakingDecelerationWalking = 2000.0f;
	Movement->JumpZVelocity = 700.0f;
	Movement->AirControl = 0.35f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 350.0f;
	CameraBoom->SetRelativeRotation(FRotator(-30.f, 0.f, 0.f));
	CameraBoom->bUsePawnControlRotation = true;
	
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->FieldOfView = 90.0f;

	CombatComponent = CreateDefaultSubobject<UDBCombatComponent>(TEXT("Combat"));
	Movement->AddTickPrerequisiteComponent(CombatComponent);
}

void ADBPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResetMovementInput();
}

void ADBPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogDBPlayer, Error, TEXT("%s requires an EnhancedInputComponent."), *GetName());
		return;
	}
	if (!MoveAction || !GamepadMoveAction || !MouseLookAction || !GamepadLookAction || !JumpAction || !WalkAction || !EvadeAction)
	{
		UE_LOG(LogDBPlayer, Error, TEXT("Assign all seven input actions on %s's Blueprint defaults."), *GetName());
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
	Input->BindAction(GamepadMoveAction, ETriggerEvent::Triggered, this, &ThisClass::MoveGamepad);
	Input->BindAction(GamepadMoveAction, ETriggerEvent::Completed, this, &ThisClass::StopGamepadMovement);
	Input->BindAction(GamepadMoveAction, ETriggerEvent::Canceled, this, &ThisClass::StopGamepadMovement);
	Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ThisClass::LookMouse);
	Input->BindAction(GamepadLookAction, ETriggerEvent::Triggered, this, &ThisClass::LookGamepad);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ACharacter::StopJumping);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &ThisClass::StartWalking);
	Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &ThisClass::StopWalking);
	Input->BindAction(WalkAction, ETriggerEvent::Canceled, this, &ThisClass::StopWalking);
	Input->BindAction(EvadeAction, ETriggerEvent::Started, CombatComponent.Get(), &UDBCombatComponent::RequestEvade);
}

void ADBPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	bUsingGamepadMovement = false;
	GetCharacterMovement()->MaxWalkSpeed = bWalkInputHeld ? WalkSpeed : RunSpeed;
	AddCameraRelativeMovement(Value.Get<FVector2D>());
}

void ADBPlayerCharacter::MoveGamepad(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D Axes = Value.Get<FVector2D>();
	if (Axes.IsNearlyZero())
	{
		StopGamepadMovement();
		return;
	}
	const float Threshold = bGamepadRunSelected ? FMath::Min(GamepadWalkThreshold, GamepadRunThreshold) : GamepadRunThreshold;
	bGamepadRunSelected = Axes.Size() >= Threshold;
	bUsingGamepadMovement = true;
	GetCharacterMovement()->MaxWalkSpeed = bGamepadRunSelected ? RunSpeed : WalkSpeed;
	AddCameraRelativeMovement(Axes.GetSafeNormal());
}

void ADBPlayerCharacter::StopGamepadMovement()
{
	bGamepadRunSelected = false;
	if (bUsingGamepadMovement)
	{
		bUsingGamepadMovement = false;
		GetCharacterMovement()->MaxWalkSpeed = bWalkInputHeld ? WalkSpeed : RunSpeed;
	}
}

void ADBPlayerCharacter::AddCameraRelativeMovement(const FVector2D& Axes)
{
	const FRotationMatrix YawRotation(FRotator(0.0, Controller->GetControlRotation().Yaw, 0.0));
	AddMovementInput(YawRotation.GetUnitAxis(EAxis::X), Axes.Y);
	AddMovementInput(YawRotation.GetUnitAxis(EAxis::Y), Axes.X);
}

void ADBPlayerCharacter::LookMouse(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>();
	AddControllerYawInput(Delta.X * MouseSensitivity.X);
	AddControllerPitchInput(Delta.Y * MouseSensitivity.Y);
}

void ADBPlayerCharacter::LookGamepad(const FInputActionValue& Value)
{
	const FVector2D Stick = Value.Get<FVector2D>();
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	AddControllerYawInput(Stick.X * GamepadLookRate.X * DeltaSeconds);
	AddControllerPitchInput(Stick.Y * GamepadLookRate.Y * DeltaSeconds);
}

void ADBPlayerCharacter::StartWalking()
{
	bWalkInputHeld = true;
	if (!bUsingGamepadMovement)
	{
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	}
}

void ADBPlayerCharacter::StopWalking()
{
	bWalkInputHeld = false;
	if (!bUsingGamepadMovement)
	{
		GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	}
}

void ADBPlayerCharacter::ResetMovementInput()
{
	bGamepadRunSelected = false;
	bUsingGamepadMovement = false;
	StopWalking();
}

void ADBPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	AddMappingContext();
	CombatComponent->AddTickPrerequisiteActor(Controller);
	if (!bInitialCameraRotationApplied && IsLocallyControlled())
	{
		// GameMode sets ControlRotation after possession; apply the starting pitch afterward.
		GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeCameraRotation);
	}
}

void ADBPlayerCharacter::InitializeCameraRotation()
{
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (bInitialCameraRotationApplied || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	FRotator ViewRotation = PlayerController->GetControlRotation();
	ViewRotation.Pitch = CameraBoom->GetRelativeRotation().Pitch;
	PlayerController->SetControlRotation(ViewRotation);
	bInitialCameraRotationApplied = true;
}

void ADBPlayerCharacter::AddMappingContext()
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

	if (!Subsystem)
	{
		return;
	}
	if (!GameplayMappingContext)
	{
		UE_LOG(LogDBPlayer, Error, TEXT("Assign GameplayMappingContext on %s's Blueprint defaults."), *GetName());
		return;
	}
	if (!Subsystem->HasMappingContext(GameplayMappingContext))
	{
		Subsystem->AddMappingContext(GameplayMappingContext, 0);
	}
}

void ADBPlayerCharacter::ReleaseMappingContext()
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Subsystem && GameplayMappingContext)
	{
		Subsystem->RemoveMappingContext(GameplayMappingContext);
	}
}

void ADBPlayerCharacter::UnPossessed()
{
	CombatComponent->ResetCombat();
	CombatComponent->RemoveTickPrerequisiteActor(Controller);
	StopJumping();
	ResetMovementInput();
	ReleaseMappingContext();
	Super::UnPossessed();
}

