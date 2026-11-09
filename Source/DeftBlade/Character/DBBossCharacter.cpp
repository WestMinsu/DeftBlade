#include "Character/DBBossCharacter.h"

#include "AI/DBEnemyAIController.h"
#include "Combat/DBHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"

ADBBossCharacter::ADBBossCharacter()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
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
	EDBAttackId Next = Distance > 300.0f ? EDBAttackId::Heavy2 : bCombo ? EDBAttackId::Heavy1 : EDBAttackId::Light1;
	if (Next == PreviousAttack)
	{
		Next = Next == EDBAttackId::Heavy1 ? EDBAttackId::Light1 : EDBAttackId::Heavy1;
	}
	PreviousAttack = Next;
	return Next;
}
