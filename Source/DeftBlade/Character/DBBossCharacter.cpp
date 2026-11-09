#include "Character/DBBossCharacter.h"

#include "AI/DBEnemyAIController.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ADBBossCharacter::ADBBossCharacter()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	for (FName Name : {TEXT("Torso"), TEXT("Legs"), TEXT("Gauntlets"), TEXT("Boots"), TEXT("Helmet")})
	{
		auto* Part = CreateDefaultSubobject<USkeletalMeshComponent>(Name);
		Part->SetupAttachment(GetMesh());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Armour.Add(Part);
	}
}

void ADBBossCharacter::SharePose()
{
	GetMesh()->SetVisibility(false, false);
	HealthText->SetVisibility(false);
	for (USkeletalMeshComponent* Part : Armour)
	{
		Part->SetLeaderPoseComponent(GetMesh(), true, false);
	}
}

void ADBBossCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SharePose();
}

void ADBBossCharacter::BeginPlay()
{
	Super::BeginPlay();
	SharePose();
	CombatComponent->OnAttackStarted.AddUObject(this, &ThisClass::StartLeap);
}

bool ADBBossCharacter::CanAct() const
{
	// Keep the committed BT task alive during this authored leap, without enabling ordinary airborne attacks.
	return Super::CanAct()
	       || (CombatComponent->GetAttackId() == EDBAttackId::BossLeap && GetCharacterMovement()->IsFlying());
}

void ADBBossCharacter::StartLeap()
{
	if (CombatComponent->GetAttackId() == EDBAttackId::BossLeap)
	{
		// Flying lets CMC apply the montage's authored vertical root motion, including collision sweeps.
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		SetActorTickEnabled(true);
	}
}

void ADBBossCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CombatComponent->GetAttackId() != EDBAttackId::BossLeap)
	{
		if (GetCharacterMovement()->IsFlying())
		{
			// Completion, parry and interruption all return to gravity. Death's disabled movement stays disabled.
			GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		}
		SetActorTickEnabled(false);
	}
}

void ADBBossCharacter::Activate(APawn* Player)
{
	if (bActive || !Player || HealthComponent->IsDead())
	{
		return;
	}
	bActive = true;
	SpawnDefaultController();
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->RememberAttacker(Player);
	}
}

float ADBBossCharacter::TakeDamage(
    float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	return bActive ? Super::TakeDamage(Amount, Event, EventInstigator, Causer) : 0.0f;
}

EDBAttackId ADBBossCharacter::SelectAttack(float Distance, bool bCombo)
{
	EDBAttackId Next;
	if (Distance > 330.0f)
	{
		Next = PreviousAttack == EDBAttackId::BossLeap ? EDBAttackId::Heavy2 : EDBAttackId::BossLeap;
	}
	else
	{
		Next = bCombo
		           ? (PreviousAttack == EDBAttackId::BossWhirlwind ? EDBAttackId::Heavy1 : EDBAttackId::BossWhirlwind)
		           : EDBAttackId::Light1;
	}
	if (Next == PreviousAttack)
	{
		Next = Next == EDBAttackId::Heavy1 ? EDBAttackId::Light1 : EDBAttackId::Heavy1;
	}
	PreviousAttack = Next;
	return Next;
}
