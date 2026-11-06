#include "Game/DBCombatEncounter.h"

#include "AI/DBEnemyAIController.h"
#include "Character/DBEnemyCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"

ADBCombatEncounter::ADBCombatEncounter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
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
	ReinforcementGate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ReinforcementGate"));
	for (UStaticMeshComponent* Gate : {EntranceGate.Get(), ExitGate.Get(), ReinforcementGate.Get()})
	{
		Gate->SetupAttachment(RootComponent);
		Gate->SetCollisionProfileName(TEXT("BlockAll"));
		Gate->SetCanEverAffectNavigation(false);
	}
}

void ADBCombatEncounter::BeginPlay()
{
	Super::BeginPlay();
	GateClosedLocation = ReinforcementGate->GetRelativeLocation();
	TSet<ADBEnemyCharacter*> BoundEnemies;
	for (ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy) && !BoundEnemies.Contains(Enemy))
		{
			BoundEnemies.Add(Enemy);
			BindEnemy(Enemy);
		}
	}
	if (BoundEnemies.IsEmpty() && Reinforcements.IsEmpty())
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
	auto* Player = Cast<ADBPlayerCharacter>(Actor);
	if (State != EDBEncounterState::Ready || !Player || !Player->IsPlayerControlled()
	    || Player->FindComponentByClass<UDBHealthComponent>()->IsDead())
	{
		return;
	}
	EncounterPlayer = Player;
	Player->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::PlayerDied);
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
	if ((State != EDBEncounterState::Active && State != EDBEncounterState::Reinforcements) || GateOpenStartedAt >= 0
	    || IsActorBeingDestroyed() || GetWorld()->bIsTearingDown || !EncounterPlayer.IsValid()
	    || EncounterPlayer->FindComponentByClass<UDBHealthComponent>()->IsDead())
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
	if (State == EDBEncounterState::Active && !Reinforcements.IsEmpty())
	{
		StartReinforcements();
		return;
	}
	SetState(EDBEncounterState::Cleared);
}

void ADBCombatEncounter::BindEnemy(ADBEnemyCharacter* Enemy)
{
	Enemy->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::CheckCleared);
	Enemy->OnDestroyed.AddDynamic(this, &ThisClass::EnemyDestroyed);
}

void ADBCombatEncounter::StartReinforcements()
{
	// Keep clear checks closed while the group is being spawned and the entrance opens.
	GateOpenStartedAt = GetWorld()->GetTimeSeconds();
	SetState(EDBEncounterState::Reinforcements);
	SetActorTickEnabled(true);
	for (const FDBReinforcementSpawn& Spawn : Reinforcements)
	{
		if (!Spawn.EnemyClass || !IsValid(Spawn.SpawnPoint))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
		    ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		auto* Enemy = GetWorld()->SpawnActor<ADBEnemyCharacter>(
		    Spawn.EnemyClass, Spawn.SpawnPoint->GetActorLocation(), Spawn.SpawnPoint->GetActorRotation(), Params);
		if (!Enemy)
		{
			continue;
		}
		Enemies.Add(Enemy);
		BindEnemy(Enemy);
		if (auto* AI = Cast<ADBEnemyAIController>(Enemy->GetController()))
		{
			// Combat noise supplies one location snapshot; normal sight must acquire the moving player.
			AI->RememberAttacker(EncounterPlayer.Get());
		}
	}
}

void ADBCombatEncounter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GateOpenStartedAt < 0)
	{
		return;
	}
	const float Alpha =
	    FMath::Clamp((GetWorld()->GetTimeSeconds() - GateOpenStartedAt) / FMath::Max(GateOpenDuration, 0.1f), 0.0, 1.0);
	ReinforcementGate->SetRelativeLocation(
	    GateClosedLocation + FVector(0, 0, GateLiftHeight * FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f)));
	if (Alpha >= 1)
	{
		GateOpenStartedAt = -1;
		SetActorTickEnabled(false);
		UpdateGates();
		CheckCleared();
	}
}

void ADBCombatEncounter::PlayerDied()
{
	GateOpenStartedAt = -1;
	SetActorTickEnabled(false);
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
	auto SetGate = [](UStaticMeshComponent* Gate, bool bClosed)
	{
		Gate->SetVisibility(bClosed);
		Gate->SetCollisionEnabled(bClosed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	};
	SetGate(EntranceGate, State == EDBEncounterState::Active || State == EDBEncounterState::Reinforcements);
	SetGate(ExitGate, State != EDBEncounterState::Cleared);
	SetGate(ReinforcementGate,
	    State != EDBEncounterState::Cleared && (State != EDBEncounterState::Reinforcements || GateOpenStartedAt >= 0));
}

void ADBCombatEncounter::EndPlay(const EEndPlayReason::Type Reason)
{
	EntryTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::Entered);
	if (EncounterPlayer.IsValid())
	{
		EncounterPlayer->FindComponentByClass<UDBHealthComponent>()->OnDeath.RemoveAll(this);
	}
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
