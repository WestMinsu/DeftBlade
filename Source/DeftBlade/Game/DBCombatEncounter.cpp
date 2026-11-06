#include "Game/DBCombatEncounter.h"

#include "Character/DBEnemyCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

ADBCombatEncounter::ADBCombatEncounter()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	EntryTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryTrigger"));
	EntryTrigger->SetupAttachment(RootComponent);
	EntryTrigger->InitBoxExtent(FVector(400, 100, 200));
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EntryTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	EntryTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EntryTrigger->SetCanEverAffectNavigation(false);
	EntranceGate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EntranceGate"));
	ExitGate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExitGate"));
	for (UStaticMeshComponent* Gate : {EntranceGate.Get(), ExitGate.Get()})
	{
		Gate->SetupAttachment(RootComponent);
		Gate->SetCollisionProfileName(TEXT("BlockAll"));
		Gate->SetCanEverAffectNavigation(false);
	}
}

void ADBCombatEncounter::BeginPlay()
{
	Super::BeginPlay();
	TSet<ADBEnemyCharacter*> BoundEnemies;
	for (ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy) && !BoundEnemies.Contains(Enemy))
		{
			BoundEnemies.Add(Enemy);
			Enemy->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::CheckCleared);
			Enemy->OnDestroyed.AddDynamic(this, &ThisClass::EnemyDestroyed);
		}
	}
	if (BoundEnemies.IsEmpty())
	{
		// A missing assignment must not trap the player in an unwinnable room.
		SetState(EDBEncounterState::Cleared);
		EntryTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}
	UpdateGates();
	EntryTrigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::Entered);
	TArray<AActor*> OverlappingPlayers;
	EntryTrigger->GetOverlappingActors(OverlappingPlayers, ADBPlayerCharacter::StaticClass());
	for (AActor* Player : OverlappingPlayers)
	{
		TryStart(Player);
	}
}

void ADBCombatEncounter::Entered(
    UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	TryStart(OtherActor);
}

void ADBCombatEncounter::TryStart(AActor* Actor)
{
	const auto* Player = Cast<ADBPlayerCharacter>(Actor);
	if (State != EDBEncounterState::Ready || !Player || !Player->IsPlayerControlled()
	    || Player->FindComponentByClass<UDBHealthComponent>()->IsDead())
	{
		return;
	}
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetState(EDBEncounterState::Active);
	CheckCleared();
}

void ADBCombatEncounter::EnemyDestroyed(AActor*)
{
	CheckCleared();
}

void ADBCombatEncounter::CheckCleared()
{
	if (State != EDBEncounterState::Active)
	{
		return;
	}
	for (const ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy) && !Enemy->IsActorBeingDestroyed()
		    && !Enemy->FindComponentByClass<UDBHealthComponent>()->IsDead())
		{
			return;
		}
	}
	SetState(EDBEncounterState::Cleared);
}

void ADBCombatEncounter::SetState(EDBEncounterState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	UpdateGates();
	OnStateChanged.Broadcast();
}

void ADBCombatEncounter::UpdateGates()
{
	EntranceGate->SetVisibility(State == EDBEncounterState::Active);
	EntranceGate->SetCollisionEnabled(
	    State == EDBEncounterState::Active ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	ExitGate->SetVisibility(State != EDBEncounterState::Cleared);
	ExitGate->SetCollisionEnabled(
	    State != EDBEncounterState::Cleared ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void ADBCombatEncounter::EndPlay(const EEndPlayReason::Type Reason)
{
	EntryTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::Entered);
	for (ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy))
		{
			Enemy->FindComponentByClass<UDBHealthComponent>()->OnDeath.RemoveAll(this);
			Enemy->OnDestroyed.RemoveDynamic(this, &ThisClass::EnemyDestroyed);
		}
	}
	Super::EndPlay(Reason);
}
