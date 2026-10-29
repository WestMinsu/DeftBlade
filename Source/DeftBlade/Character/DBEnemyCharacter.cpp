#include "Character/DBEnemyCharacter.h"

#include "AI/DBEnemyAIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/DBCollisionChannels.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ADBEnemyCharacter::ADBEnemyCharacter()
{
	AIControllerClass = ADBEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	GetCapsuleComponent()->InitCapsuleSize(42, 96);
	GetCapsuleComponent()->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Overlap);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	auto* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0, 540, 0);
	Movement->MaxWalkSpeed = 340;
	Movement->GravityScale = 2.0f;
	CombatComponent = CreateDefaultSubobject<UDBCombatComponent>(TEXT("Combat"));
	HealthComponent = CreateDefaultSubobject<UDBHealthComponent>(TEXT("Health"));
	Movement->AddTickPrerequisiteComponent(CombatComponent);
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HealthText"));
	HealthText->SetupAttachment(GetRootComponent());
	HealthText->SetRelativeLocation(FVector(0, 0, 125));
	HealthText->SetHorizontalAlignment(EHTA_Center);
	HealthText->SetWorldSize(22);
	HealthText->SetTextRenderColor(FColor(255, 85, 65));
}

void ADBEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	CombatComponent->SetWeaponMesh(WeaponMesh);
	CombatComponent->OnParried.AddUObject(this, &ThisClass::ReactToParry);
	HealthComponent->OnHealthChanged.AddUObject(this, &ThisClass::UpdateHealthText);
	HealthComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	UpdateHealthText(HealthComponent->GetHealth(), HealthComponent->GetMaxHealth());
}

bool ADBEnemyCharacter::CanAct() const
{
	return !HealthComponent->IsDead() && ReactionInstanceId == INDEX_NONE && GetCharacterMovement()->IsMovingOnGround();
}

void ADBEnemyCharacter::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (GetCharacterMovement()->IsFalling())
	{
		StopReaction();
	}
	else if (GetCharacterMovement()->IsMovingOnGround())
	{
		CombatComponent->HandleLanding();
	}
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RefreshCombatState();
	}
}

float ADBEnemyCharacter::TakeDamage(
    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	const float Applied = HealthComponent->ApplyDamage(Amount);
	if (Applied <= 0)
	{
		return 0;
	}
	Super::TakeDamage(Applied, Event, EventInstigator, Causer);
	if (HealthComponent->IsDead())
	{
		return Applied;
	}
	const auto* AttackerCombat = Causer ? Causer->FindComponentByClass<UDBCombatComponent>() : nullptr;
	const EDBAttackId Attack = AttackerCombat ? AttackerCombat->GetAttackId() : EDBAttackId::None;
	const bool bForceReaction = Attack == EDBAttackId::Launcher || Attack == EDBAttackId::AirHeavy;
	if (!bForceReaction && GetCharacterMovement()->IsMovingOnGround()
	    && (ReactionInstanceId != INDEX_NONE || GetWorld()->GetTimeSeconds() < NextHitReactionAt))
	{
		return Applied;
	}
	PlayHitReaction(EventInstigator ? EventInstigator->GetPawn() : Causer, HitReactionMontage);
	return Applied;
}

void ADBEnemyCharacter::ReactToParry(AActor* Defender)
{
	PlayHitReaction(Defender, ParryReactionMontage ? ParryReactionMontage : HitReactionMontage);
}

void ADBEnemyCharacter::PlayHitReaction(AActor* Attacker, UAnimMontage* Montage)
{
	CombatComponent->ResetCombat();
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->StopMovement();
		AI->RememberAttacker(Attacker);
	}
	const double FallingSpeed = GetCharacterMovement()->IsFalling() ? GetVelocity().Z : 0.0;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->Velocity.Z = FallingSpeed;
	StopReaction();
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	    Anim && GetCharacterMovement()->IsMovingOnGround() && Montage && Anim->Montage_Play(Montage) > 0)
	{
		auto* Instance = Anim->GetActiveInstanceForMontage(Montage);
		ReactionInstanceId = Instance->GetInstanceID();
		Instance->OnMontageEnded.BindUObject(this, &ThisClass::ReactionEnded, ReactionInstanceId);
	}
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RefreshCombatState();
	}
}

void ADBEnemyCharacter::StopReaction()
{
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (auto* Instance = Anim->GetMontageInstanceForID(ReactionInstanceId))
		{
			Instance->OnMontageEnded.Unbind();
			Anim->Montage_Stop(0.08f, Instance->Montage);
		}
	}
	ReactionInstanceId = INDEX_NONE;
}

void ADBEnemyCharacter::ReactionEnded(UAnimMontage*, bool, int32 InstanceId)
{
	if (InstanceId != ReactionInstanceId)
	{
		return;
	}
	ReactionInstanceId = INDEX_NONE;
	NextHitReactionAt = GetWorld()->GetTimeSeconds() + HitReactionCooldown;
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RefreshCombatState();
	}
}

void ADBEnemyCharacter::UpdateHealthText(float Current, float Maximum)
{
	HealthText->SetText(FText::FromString(FString::Printf(TEXT("HP %.0f / %.0f"), Current, Maximum)));
}

void ADBEnemyCharacter::HandleDeath()
{
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->StopBehavior();
	}
	CombatComponent->ResetCombat();
	StopReaction();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthText->SetText(FText::FromString(TEXT("DOWN")));
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetCollisionResponseToChannel(DBCollision::Melee, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetSimulatePhysics(true);
}

void ADBEnemyCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	StopReaction();
	Super::EndPlay(Reason);
}
