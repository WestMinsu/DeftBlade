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
	HealthComponent->OnHealthChanged.AddUObject(this, &ThisClass::UpdateHealthText);
	HealthComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	UpdateHealthText(HealthComponent->GetHealth(), HealthComponent->GetMaxHealth());
}

bool ADBEnemyCharacter::CanAct() const
{
	return !HealthComponent->IsDead() && ReactionInstanceId == INDEX_NONE;
}

float ADBEnemyCharacter::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	const float Applied = HealthComponent->ApplyDamage(Amount);
	if (Applied <= 0) { return 0; }
	Super::TakeDamage(Applied, Event, EventInstigator, Causer);
	if (HealthComponent->IsDead()) { return Applied; }
	CombatComponent->ResetCombat();
	if (auto* AI = Cast<ADBEnemyAIController>(GetController()))
	{
		AI->StopMovement();
		AI->RememberAttacker(EventInstigator ? EventInstigator->GetPawn() : Causer);
	}
	GetCharacterMovement()->StopMovementImmediately();
	StopReaction();
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance(); Anim && HitReactionMontage
		&& Anim->Montage_Play(HitReactionMontage) > 0)
	{
		auto* Instance = Anim->GetActiveInstanceForMontage(HitReactionMontage);
		ReactionInstanceId = Instance->GetInstanceID();
		Instance->OnMontageEnded.BindUObject(this, &ThisClass::ReactionEnded, ReactionInstanceId);
	}
	if (auto* AI = Cast<ADBEnemyAIController>(GetController())) { AI->RefreshCombatState(); }
	return Applied;
}

void ADBEnemyCharacter::StopReaction()
{
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (auto* Instance = Anim->GetMontageInstanceForID(ReactionInstanceId))
		{
			Instance->OnMontageEnded.Unbind();
			Anim->Montage_Stop(0.08f, HitReactionMontage);
		}
	}
	ReactionInstanceId = INDEX_NONE;
}

void ADBEnemyCharacter::ReactionEnded(UAnimMontage*, bool, int32 InstanceId)
{
	if (InstanceId != ReactionInstanceId) { return; }
	ReactionInstanceId = INDEX_NONE;
	if (auto* AI = Cast<ADBEnemyAIController>(GetController())) { AI->RefreshCombatState(); }
}

void ADBEnemyCharacter::UpdateHealthText(float Current, float Maximum)
{
	HealthText->SetText(FText::FromString(FString::Printf(TEXT("HP %.0f / %.0f"), Current, Maximum)));
}

void ADBEnemyCharacter::HandleDeath()
{
	if (auto* AI = Cast<ADBEnemyAIController>(GetController())) { AI->StopBehavior(); }
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
