#include "Character/DBPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBCollisionChannels.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBTargetingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDBPlayer, Log, All);

ADBPlayerCharacter::ADBPlayerCharacter()
{
	JumpMaxCount = 2;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Overlap);
	GetMesh()->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Ignore);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
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
	HealthComponent = CreateDefaultSubobject<UDBHealthComponent>(TEXT("Health"));
	TargetingComponent = CreateDefaultSubobject<UDBTargetingComponent>(TEXT("Targeting"));
	CombatComponent->AddTickPrerequisiteComponent(TargetingComponent);
	Movement->AddTickPrerequisiteComponent(CombatComponent);
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADBPlayerCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CombatComponent->InitializeWeapon(WeaponMesh);
}

void ADBPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	CombatComponent->InitializeWeapon(WeaponMesh);
	CombatComponent->OnAttackStarted.AddUObject(this, &ThisClass::AssistAttackFacing);
	HealthComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	ResetMovementInput();
}

void ADBPlayerCharacter::AssistAttackFacing()
{
	const APlayerController* PC = Cast<APlayerController>(Controller);
	const UEnhancedPlayerInput* Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
	FVector Direction = GetActorForwardVector();
	if (Input)
	{
		const FVector2D Keyboard =
		    MoveAction ? Input->GetActionValue(MoveAction).Get<FVector2D>() : FVector2D::ZeroVector;
		const FVector2D Gamepad =
		    GamepadMoveAction ? Input->GetActionValue(GamepadMoveAction).Get<FVector2D>() : FVector2D::ZeroVector;
		const FVector2D Axes = Keyboard + Gamepad.GetSafeNormal();
		if (!Axes.IsNearlyZero())
		{
			const FRotationMatrix Yaw(FRotator(0, PC->GetControlRotation().Yaw, 0));
			Direction = Yaw.GetUnitAxis(EAxis::X) * Axes.Y + Yaw.GetUnitAxis(EAxis::Y) * Axes.X;
		}
	}
	// Read the current action values: combat may already have consumed CMC's pending movement this frame.
	TargetingComponent->AssistAttackFacing(Direction);
}

float ADBPlayerCharacter::TakeDamage(
    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	const float Applied = HealthComponent->ApplyDamage(Amount);
	if (Applied > 0.0f)
	{
		Super::TakeDamage(Applied, Event, EventInstigator, Causer);
		if (!HealthComponent->IsDead())
		{
			StopJumpMontages();
			StopJumping();
			StopSprinting();
			CombatComponent->ReactToHit(HitReactionMontage);
		}
	}
	return Applied;
}

void ADBPlayerCharacter::HandleDeath()
{
	TargetingComponent->ClearLockOn();
	CombatComponent->ResetCombat();
	StopJumpMontages();
	StopJumping();
	ResetMovementInput();
	GetCharacterMovement()->DisableMovement();
}

bool ADBPlayerCharacter::CanJumpWhileFalling() const
{
	// Walking off a ledge does not consume the first jump.
	return true;
}

bool ADBPlayerCharacter::CanJumpInternal_Implementation() const
{
	return !HealthComponent->IsDead() && CombatComponent->CanJumpFromAction()
	       && Super::CanJumpInternal_Implementation();
}

void ADBPlayerCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	StopSprinting();
	// ACharacter has already accepted the jump and incremented its count.
	if (JumpCurrentCount != 2)
	{
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	const UEnhancedPlayerInput* Input =
	    PlayerController ? Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput) : nullptr;
	// Read current actions: velocity can persist after release, and evade consumes movement intent before CMC.
	const bool bHasDirectionInput = Input
	                                && ((MoveAction && Input->GetActionValue(MoveAction).IsNonZero())
	                                    || (GamepadMoveAction && Input->GetActionValue(GamepadMoveAction).IsNonZero()));
	UAnimMontage* Montage = bHasDirectionInput ? DoubleJumpMontage.Get() : BackwardDoubleJumpMontage.Get();
	if (Montage)
	{
		PlayAnimMontage(Montage);
	}
}

void ADBPlayerCharacter::Landed(const FHitResult& Hit)
{
	CombatComponent->HandleLanding();
	Super::Landed(Hit);
	StopJumpMontages();
}

void ADBPlayerCharacter::StopJumpMontages()
{
	for (UAnimMontage* Montage : {DoubleJumpMontage.Get(), BackwardDoubleJumpMontage.Get()})
	{
		if (Montage)
		{
			StopAnimMontage(Montage);
		}
	}
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
	if (!MoveAction || !GamepadMoveAction || !MouseLookAction || !GamepadLookAction || !JumpAction || !WalkAction
	    || !EvadeAction || !LightAttackAction || !HeavyAttackAction || !LockOnAction || !SwitchTargetAction)
	{
		UE_LOG(LogDBPlayer, Error, TEXT("Assign all eleven input actions on %s's Blueprint defaults."), *GetName());
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
	Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ThisClass::StopSprinting);
	Input->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ThisClass::StopSprinting);
	Input->BindAction(GamepadMoveAction, ETriggerEvent::Triggered, this, &ThisClass::MoveGamepad);
	Input->BindAction(GamepadMoveAction, ETriggerEvent::Completed, this, &ThisClass::StopGamepadMovement);
	Input->BindAction(GamepadMoveAction, ETriggerEvent::Canceled, this, &ThisClass::StopGamepadMovement);
	Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ThisClass::LookMouse);
	Input->BindAction(GamepadLookAction, ETriggerEvent::Triggered, this, &ThisClass::LookGamepad);
	Input->BindAction(GamepadLookAction, ETriggerEvent::Completed, this, &ThisClass::StopLookGamepad);
	Input->BindAction(GamepadLookAction, ETriggerEvent::Canceled, this, &ThisClass::StopLookGamepad);
	Input->BindAction(LockOnAction, ETriggerEvent::Started, this, &ThisClass::ToggleLockOn);
	Input->BindAction(SwitchTargetAction, ETriggerEvent::Triggered, this, &ThisClass::SwitchTarget);
	Input->BindAction(SwitchTargetAction, ETriggerEvent::Completed, this, &ThisClass::StopSwitchTarget);
	Input->BindAction(SwitchTargetAction, ETriggerEvent::Canceled, this, &ThisClass::StopSwitchTarget);
	Input->BindAction(JumpAction, ETriggerEvent::Started, CombatComponent.Get(), &UDBCombatComponent::RequestJump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ACharacter::StopJumping);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &ThisClass::StartWalking);
	Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &ThisClass::StopWalking);
	Input->BindAction(WalkAction, ETriggerEvent::Canceled, this, &ThisClass::StopWalking);
	Input->BindAction(EvadeAction, ETriggerEvent::Started, CombatComponent.Get(), &UDBCombatComponent::RequestEvade);
	Input->BindAction(EvadeAction, ETriggerEvent::Completed, this, &ThisClass::StopSprinting);
	Input->BindAction(EvadeAction, ETriggerEvent::Canceled, this, &ThisClass::StopSprinting);
	Input->BindAction(LightAttackAction, ETriggerEvent::Started, this, &ThisClass::LightAttack);
	Input->BindAction(HeavyAttackAction, ETriggerEvent::Started, this, &ThisClass::HeavyAttack);
}

void ADBPlayerCharacter::LightAttack()
{
	StopSprinting();
	CombatComponent->RequestAttack(false);
}

void ADBPlayerCharacter::HeavyAttack()
{
	StopSprinting();
	CombatComponent->RequestAttack(true);
}

void ADBPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	bUsingGamepadMovement = false;
	AddDirectionalMovement(Value.Get<FVector2D>());
	UpdateSprint();
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
	const float Threshold =
	    bGamepadRunSelected ? FMath::Min(GamepadWalkThreshold, GamepadRunThreshold) : GamepadRunThreshold;
	bGamepadRunSelected = Axes.Size() >= Threshold;
	bUsingGamepadMovement = true;
	AddDirectionalMovement(Axes.GetSafeNormal());
	UpdateSprint();
}

void ADBPlayerCharacter::StopGamepadMovement()
{
	bGamepadRunSelected = false;
	if (bUsingGamepadMovement)
	{
		bUsingGamepadMovement = false;
		StopSprinting();
	}
}

void ADBPlayerCharacter::AddDirectionalMovement(const FVector2D& Axes)
{
	if (HealthComponent->IsDead())
	{
		return;
	}
	const FRotationMatrix YawRotation(FRotator(0.0, Controller->GetControlRotation().Yaw, 0.0));
	AddMovementInput(YawRotation.GetUnitAxis(EAxis::X), Axes.Y);
	AddMovementInput(YawRotation.GetUnitAxis(EAxis::Y), Axes.X);
}

void ADBPlayerCharacter::LookMouse(const FInputActionValue& Value)
{
	if (TargetingComponent->IsLockedOn())
	{
		return;
	}
	const FVector2D Delta = Value.Get<FVector2D>();
	AddControllerYawInput(Delta.X * MouseSensitivity.X);
	AddControllerPitchInput(Delta.Y * MouseSensitivity.Y);
}

void ADBPlayerCharacter::LookGamepad(const FInputActionValue& Value)
{
	const FVector2D Stick = Value.Get<FVector2D>();
	if (TargetingComponent->IsLockedOn())
	{
		TargetingComponent->HandleStickSwitch(Stick.X);
		return;
	}
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	AddControllerYawInput(Stick.X * GamepadLookRate.X * DeltaSeconds);
	AddControllerPitchInput(Stick.Y * GamepadLookRate.Y * DeltaSeconds);
}

void ADBPlayerCharacter::StopLookGamepad()
{
	TargetingComponent->HandleStickSwitch(0);
}

void ADBPlayerCharacter::ToggleLockOn()
{
	TargetingComponent->ToggleLockOn();
	if (TargetingComponent->IsLockedOn())
	{
		StopSprinting();
		const auto* PC = Cast<APlayerController>(Controller);
		const auto* Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
		if (Input && GamepadLookAction)
		{
			TargetingComponent->HandleStickSwitch(Input->GetActionValue(GamepadLookAction).Get<FVector2D>().X);
		}
	}
}

void ADBPlayerCharacter::SwitchTarget(const FInputActionValue& Value)
{
	const float Direction = FMath::Sign(Value.Get<float>());
	if (Direction != SwitchInputDirection)
	{
		SwitchInputDirection = Direction;
		TargetingComponent->SwitchTarget(Direction);
	}
}

void ADBPlayerCharacter::StopSwitchTarget()
{
	SwitchInputDirection = 0;
}

void ADBPlayerCharacter::StartWalking()
{
	bWalkInputHeld = true;
	if (!bUsingGamepadMovement)
	{
		StopSprinting();
	}
}

void ADBPlayerCharacter::StopWalking()
{
	bWalkInputHeld = false;
	if (!bUsingGamepadMovement)
	{
		RefreshMovementSpeed();
	}
}

void ADBPlayerCharacter::UpdateSprint()
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	const UEnhancedPlayerInput* Input =
	    PlayerController ? Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput) : nullptr;
	const FInputActionInstance* EvadeInput =
	    Input && EvadeAction ? Input->FindActionInstanceData(EvadeAction) : nullptr;
	// A fresh press belongs to evade/chaining; sprint requires a continued hold.
	const bool bHeld = !(bWalkInputHeld && !bUsingGamepadMovement) && EvadeInput && EvadeInput->GetValue().IsNonZero()
	                   && EvadeInput->GetTriggeredTime() >= 0.15f;
	CombatComponent->SetEvadeHeld(bHeld, SprintSpeed);
	const bool bCanSprint = bHeld && GetCharacterMovement()->IsMovingOnGround();
	bSprinting = bCanSprint && (bSprinting || CombatComponent->TryTransitionToSprint());
	RefreshMovementSpeed();
}

void ADBPlayerCharacter::StopSprinting()
{
	CombatComponent->SetEvadeHeld(false, SprintSpeed);
	bSprinting = false;
	RefreshMovementSpeed();
}

void ADBPlayerCharacter::RefreshMovementSpeed()
{
	const bool bWalk = bUsingGamepadMovement ? !bGamepadRunSelected : bWalkInputHeld;
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : (bWalk ? WalkSpeed : RunSpeed);
}

void ADBPlayerCharacter::ResetMovementInput()
{
	StopSwitchTarget();
	bGamepadRunSelected = false;
	bUsingGamepadMovement = false;
	StopSprinting();
	StopWalking();
}

void ADBPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	AddMappingContext();
	CombatComponent->AddTickPrerequisiteActor(Controller);
	TargetingComponent->AddTickPrerequisiteActor(Controller);
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
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
	    LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

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
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
	    LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Subsystem && GameplayMappingContext)
	{
		Subsystem->RemoveMappingContext(GameplayMappingContext);
	}
}

void ADBPlayerCharacter::UnPossessed()
{
	TargetingComponent->ClearLockOn();
	TargetingComponent->RemoveTickPrerequisiteActor(Controller);
	CombatComponent->ResetCombat();
	CombatComponent->RemoveTickPrerequisiteActor(Controller);
	StopJumpMontages();
	StopJumping();
	ResetMovementInput();
	ReleaseMappingContext();
	Super::UnPossessed();
}
