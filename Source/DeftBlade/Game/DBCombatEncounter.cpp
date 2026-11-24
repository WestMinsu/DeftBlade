#include "Game/DBCombatEncounter.h"

#include "AI/DBEnemyAIController.h"
#include "Character/DBEnemyCharacter.h"
#include "Character/DBBossCharacter.h"
#include "Character/DBPlayerCharacter.h"
#include "Combat/DBHealthComponent.h"
#include "Combat/DBCombatComponent.h"
#include "Combat/DBCombatScoreComponent.h"
#include "Combat/DBTrainingDummy.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "UI/DBPlayerHUD.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float GroupArrivalLeadTime = 0.4f;
	constexpr double GroupPairInterval = 0.5;
	constexpr int32 GroupPairSize = 2;
}

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
	GroupReinforcementGate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroupReinforcementGate"));
	BossGate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BossGate"));
	for (UStaticMeshComponent* Gate :
	    {EntranceGate.Get(), ExitGate.Get(), ReinforcementGate.Get(), GroupReinforcementGate.Get(), BossGate.Get()})
	{
		Gate->SetupAttachment(RootComponent);
		Gate->SetCollisionProfileName(TEXT("BlockAll"));
		Gate->SetCanEverAffectNavigation(false);
	}
}

void ADBCombatEncounter::BeginPlay()
{
	Super::BeginPlay();
	if (IsValid(Boss))
	{
		Boss->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::BossDefeated);
	}
	GateClosedLocation = ReinforcementGate->GetRelativeLocation();
	GroupGateClosedLocation = GroupReinforcementGate->GetRelativeLocation();
	if (IsValid(FinishArea))
	{
		FinishArea->SetActorEnableCollision(false);
		FinishArea->OnActorBeginOverlap.AddDynamic(this, &ThisClass::FinishEntered);
	}
	if (bRestoredBossCheckpoint)
	{
		UpdateGates();
		return;
	}
	TSet<ADBEnemyCharacter*> BoundEnemies;
	for (ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy) && !BoundEnemies.Contains(Enemy))
		{
			BoundEnemies.Add(Enemy);
			BindEnemy(Enemy);
		}
	}
	if (BoundEnemies.IsEmpty() && !DropReinforcement.EnemyClass && !ParryReinforcement.EnemyClass
	    && Reinforcements.IsEmpty())
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

ATargetPoint* ADBCombatEncounter::GetBossCheckpoint() const
{
	return IsValid(Boss) && IsValid(FinishArea) && IsValid(BossCheckpoint) ? BossCheckpoint.Get() : nullptr;
}

int32 ADBCombatEncounter::GetBossRetryScore() const
{
	if (!GetBossCheckpoint())
	{
		return INDEX_NONE;
	}
	if (State == EDBEncounterState::Cleared && EncounterPlayer.IsValid())
	{
		return EncounterPlayer->FindComponentByClass<UDBCombatScoreComponent>()->GetScore();
	}
	return State == EDBEncounterState::BossActive || State == EDBEncounterState::BossIntroduction ? BossCheckpointScore
	                                                                                              : INDEX_NONE;
}

void ADBCombatEncounter::RestoreBossCheckpoint(ADBPlayerCharacter* Player, int32 Score)
{
	BindPlayer(Player);
	bRestoredBossCheckpoint = true;
	Player->FindComponentByClass<UDBCombatScoreComponent>()->RestoreCheckpointScore(Score);
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy) && Enemy != Boss.Get())
		{
			Enemy->Destroy();
		}
	}
	Enemies.Reset();
	SetState(EDBEncounterState::Cleared);
}

EDBTraversalLesson ADBCombatEncounter::GetTraversalLesson(const FVector& Position) const
{
	const auto IsInside = [&Position](const ATriggerBox* Area)
	{
		const auto* Box = IsValid(Area) ? Cast<UBoxComponent>(Area->GetCollisionComponent()) : nullptr;
		if (!Box)
		{
			return false;
		}
		const FVector Extent = Box->GetUnscaledBoxExtent();
		return FBox(-Extent, Extent).IsInsideOrOn(Box->GetComponentTransform().InverseTransformPosition(Position));
	};
	if (IsInside(JumpArea))
	{
		return EDBTraversalLesson::Jump;
	}
	if (IsInside(DoubleJumpArea))
	{
		return EDBTraversalLesson::DoubleJump;
	}
	return IsInside(EvadeSprintArea) ? EDBTraversalLesson::EvadeSprint : EDBTraversalLesson::None;
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
	BindPlayer(Player);
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetState(EDBEncounterState::Active);
	CheckCleared();
}

void ADBCombatEncounter::EnemyDestroyed(AActor*)
{
	CheckCleared();
}

void ADBCombatEncounter::FinishEntered(AActor*, AActor* OtherActor)
{
	auto* Player = Cast<ADBPlayerCharacter>(OtherActor);
	if (State != EDBEncounterState::Cleared || !Player || !Player->IsPlayerControlled()
	    || Player->FindComponentByClass<UDBHealthComponent>()->IsDead())
	{
		return;
	}
	BindPlayer(Player);
	Player->FindComponentByClass<UDBCombatComponent>()->ResetCombat();
	Player->StopJumping();
	Player->GetCharacterMovement()->StopMovementImmediately();
	if (IsValid(Boss) && !Boss->FindComponentByClass<UDBHealthComponent>()->IsDead())
	{
		BossCheckpointScore = Player->FindComponentByClass<UDBCombatScoreComponent>()->GetScore();
		if (bRestoredBossCheckpoint)
		{
			StartBossFight();
		}
		else
		{
			StartBossIntroduction();
		}
		return;
	}
	SetState(EDBEncounterState::Completed);
}

void ADBCombatEncounter::BindPlayer(ADBPlayerCharacter* Player)
{
	if (EncounterPlayer.Get() == Player)
	{
		return;
	}
	if (EncounterPlayer.IsValid())
	{
		EncounterPlayer->FindComponentByClass<UDBHealthComponent>()->OnDeath.RemoveAll(this);
	}
	EncounterPlayer = Player;
	Player->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::PlayerDied);
}

float ADBCombatEncounter::GetBossIntroductionProgress() const
{
	return BossIntroductionStartedAt >= 0 ? FMath::Clamp((GetWorld()->GetTimeSeconds() - BossIntroductionStartedAt)
	                                                         / FMath::Max(BossIntroductionDuration, 2.f),
	                                            0., 1.)
	                                      : -1.f;
}

void ADBCombatEncounter::StartBossIntroduction()
{
	auto* PC = Cast<APlayerController>(EncounterPlayer->GetController());
	if (!PC)
	{
		return;
	}
	EncounterPlayer->ResetGameplayInput();
	IntroductionController = PC;
	PreviousViewTarget = PC->GetViewTarget();
	IntroductionCamera = GetWorld()->SpawnActor<ACameraActor>();
	if (!IntroductionCamera)
	{
		FinishBossIntroduction(true);
		return;
	}
	IntroductionCamera->GetCameraComponent()->FieldOfView = 48.f;
	IntroductionCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	IntroductionCamera->GetCameraComponent()->PostProcessBlendWeight = 0.f;
	PC->SetCinematicMode(true, false, false, true, true);
	BossIntroductionStartedAt = GetWorld()->GetTimeSeconds();
	bReturningFromIntroduction = false;
	SetState(EDBEncounterState::BossIntroduction);
	SetActorTickEnabled(true);
	UpdateBossIntroduction();
	PC->SetViewTargetWithBlend(IntroductionCamera, .3f, VTBlend_Cubic);
	if (BossIntroductionMontage)
	{
		if (UAnimInstance* Anim = Boss->GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Play(BossIntroductionMontage);
		}
	}
}

void ADBCombatEncounter::UpdateBossIntroduction()
{
	APlayerController* PC = IntroductionController.Get();
	if (!PC || !EncounterPlayer.IsValid() || PC->GetPawn() != EncounterPlayer.Get() || !IsValid(Boss))
	{
		FinishBossIntroduction(false);
		SetState(EDBEncounterState::Cleared);
		return;
	}
	const float Progress = GetBossIntroductionProgress();
	const float Move = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(Progress / .85f, 0.f, 1.f), 2.f);
	const FVector Forward = Boss->GetActorForwardVector(), Right = Boss->GetActorRightVector();
	const FVector Location = Boss->GetActorLocation() + Forward * FMath::Lerp(340.f, 285.f, Move)
	                         + Right * FMath::Lerp(155.f, 110.f, Move) - FVector(0, 0, 20.f);
	const FVector Focus = Boss->GetActorLocation() + Right * 55.f + FVector(0, 0, 20.f);
	IntroductionCamera->SetActorLocationAndRotation(Location, (Focus - Location).Rotation());
	if (!bReturningFromIntroduction && Progress >= .85f)
	{
		bReturningFromIntroduction = true;
		PC->SetViewTargetWithBlend(PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : EncounterPlayer.Get(),
		    FMath::Max(BossIntroductionDuration, 2.f) * .15f, VTBlend_Cubic);
	}
	if (Progress >= 1.f)
	{
		FinishBossIntroduction(true);
	}
}

void ADBCombatEncounter::FinishBossIntroduction(bool bStartFight)
{
	// Release exactly the cinematic input lock we installed, including abort/unload paths.
	if (APlayerController* PC = IntroductionController.Get())
	{
		PC->SetCinematicMode(false, false, false, true, true);
		PC->FlushPressedKeys();
		if (!GetWorld()->bIsTearingDown)
		{
			PC->SetViewTarget(PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : PC->GetPawn());
		}
	}
	if (IsValid(Boss) && BossIntroductionMontage)
	{
		if (UAnimInstance* Anim = Boss->GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(.15f, BossIntroductionMontage);
		}
	}
	if (IntroductionCamera)
	{
		IntroductionCamera->Destroy();
		IntroductionCamera = nullptr;
	}
	IntroductionController.Reset();
	PreviousViewTarget.Reset();
	BossIntroductionStartedAt = -1;
	SetActorTickEnabled(false);
	if (bStartFight && IsValid(Boss) && EncounterPlayer.IsValid())
	{
		StartBossFight();
	}
}

void ADBCombatEncounter::StartBossFight()
{
	SetState(EDBEncounterState::BossActive);
	Boss->Activate(EncounterPlayer.Get());
}

void ADBCombatEncounter::BossDefeated()
{
	if (State != EDBEncounterState::BossActive || GetWorld()->bIsTearingDown || !EncounterPlayer.IsValid()
	    || EncounterPlayer->FindComponentByClass<UDBHealthComponent>()->IsDead())
	{
		return;
	}
	auto* Player = EncounterPlayer.Get();
	Player->FindComponentByClass<UDBCombatComponent>()->ResetCombat();
	Player->StopJumping();
	Player->GetCharacterMovement()->StopMovementImmediately();
	SetState(EDBEncounterState::Completed);
}

void ADBCombatEncounter::CheckCleared()
{
	if ((State != EDBEncounterState::Active && State != EDBEncounterState::AirReinforcement
	        && State != EDBEncounterState::ParryReinforcement && State != EDBEncounterState::Reinforcements)
	    || GateOpenStartedAt >= 0 || IsActorBeingDestroyed() || GetWorld()->bIsTearingDown || !EncounterPlayer.IsValid()
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
	if (State == EDBEncounterState::Active && SpawnReinforcement(DropReinforcement, true))
	{
		SetState(EDBEncounterState::AirReinforcement);
		return;
	}
	if ((State == EDBEncounterState::Active || State == EDBEncounterState::AirReinforcement)
	    && ParryReinforcement.EnemyClass && IsValid(ParryReinforcement.SpawnPoint))
	{
		StartReinforcements(EDBEncounterState::ParryReinforcement);
		return;
	}
	if (State != EDBEncounterState::Reinforcements && !Reinforcements.IsEmpty())
	{
		StartReinforcements(EDBEncounterState::Reinforcements);
		return;
	}
	SetState(EDBEncounterState::Cleared);
}

void ADBCombatEncounter::BindEnemy(ADBEnemyCharacter* Enemy)
{
	Enemy->FindComponentByClass<UDBHealthComponent>()->OnDeath.AddUObject(this, &ThisClass::CheckCleared);
	Enemy->OnDestroyed.AddDynamic(this, &ThisClass::EnemyDestroyed);
}

void ADBCombatEncounter::StartReinforcements(EDBEncounterState NewState)
{
	const bool bStageGroup = NewState == EDBEncounterState::Reinforcements && GroupReinforcementGate->GetStaticMesh();
	if (bStageGroup || State != EDBEncounterState::ParryReinforcement)
	{
		GateOpenStartedAt = GetWorld()->GetTimeSeconds();
		SetActorTickEnabled(true);
	}
	SetState(NewState);
	if (NewState == EDBEncounterState::ParryReinforcement)
	{
		SpawnReinforcement(ParryReinforcement, false);
	}
	else
	{
		for (const FDBReinforcementSpawn& Spawn : Reinforcements)
		{
			if (ADBEnemyCharacter* Enemy = SpawnReinforcement(Spawn, false, bStageGroup); Enemy && bStageGroup)
			{
				StagedReinforcements.Add(Enemy);
			}
		}
		if (bStageGroup && GroupArrivalSound && !StagedReinforcements.IsEmpty())
		{
			UGameplayStatics::PlaySoundAtLocation(
			    this, GroupArrivalSound, GroupReinforcementGate->GetComponentLocation());
		}
	}
	if (GateOpenStartedAt < 0)
	{
		CheckCleared();
	}
}

ADBEnemyCharacter* ADBCombatEncounter::SpawnReinforcement(const FDBReinforcementSpawn& Spawn, bool bDrop, bool bStage)
{
	if (!Spawn.EnemyClass || !IsValid(Spawn.SpawnPoint))
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	Params.bDeferConstruction = bStage;
	const FTransform SpawnTransform(Spawn.SpawnPoint->GetActorRotation(), Spawn.SpawnPoint->GetActorLocation());
	auto* Enemy = GetWorld()->SpawnActor<ADBEnemyCharacter>(Spawn.EnemyClass, SpawnTransform, Params);
	if (!Enemy)
	{
		return nullptr;
	}
	if (bStage)
	{
		Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
		Enemy->FinishSpawning(SpawnTransform);
		if (!IsValid(Enemy))
		{
			return nullptr;
		}
	}
	Enemies.Add(Enemy);
	BindEnemy(Enemy);
	if (bDrop)
	{
		Enemy->BeginDropArrival();
	}
	else if (IsValid(TrainingDummy)
	         && (State == EDBEncounterState::ParryReinforcement || !ParryReinforcement.EnemyClass))
	{
		auto* Combat = Enemy->FindComponentByClass<UDBCombatComponent>();
		const auto* PC = Cast<APlayerController>(EncounterPlayer->GetController());
		if (const UDBWeaponData* Weapon = Combat->GetWeaponData(); PC && Weapon && Weapon->AttackProtectionMaterial)
		{
			if (auto* HUD = PC->GetHUD<ADBPlayerHUD>())
			{
				Combat->OnTutorialParryContact.BindUObject(HUD, &ADBPlayerHUD::ShowParryLesson);
			}
		}
	}
	if (auto* AI = Cast<ADBEnemyAIController>(Enemy->GetController()))
	{
		// Combat noise supplies one location snapshot; normal sight must acquire the moving player.
		AI->RememberAttacker(EncounterPlayer.Get());
	}
	return Enemy;
}

void ADBCombatEncounter::ReleaseStagedReinforcements(int32 Count)
{
	const int32 ReleaseCount = FMath::Min(Count, StagedReinforcements.Num());
	for (int32 Index = 0; Index < ReleaseCount; ++Index)
	{
		ADBEnemyCharacter* Enemy = StagedReinforcements[Index].Get();
		if (Enemy && !Enemy->FindComponentByClass<UDBHealthComponent>()->IsDead())
		{
			Enemy->SpawnDefaultController();
			if (auto* AI = Cast<ADBEnemyAIController>(Enemy->GetController()))
			{
				AI->RememberAttacker(EncounterPlayer.Get());
			}
		}
	}
	StagedReinforcements.RemoveAt(0, ReleaseCount);
}

void ADBCombatEncounter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (BossIntroductionStartedAt >= 0)
	{
		UpdateBossIntroduction();
		return;
	}
	if (GateOpenStartedAt < 0)
	{
		return;
	}
	const bool bOpeningGroupGate =
	    State == EDBEncounterState::Reinforcements && GroupReinforcementGate->GetStaticMesh();
	const double Now = GetWorld()->GetTimeSeconds();
	const double LeadTime = bOpeningGroupGate ? GroupArrivalLeadTime : 0.0;
	const float Alpha =
	    FMath::Clamp((Now - GateOpenStartedAt - LeadTime) / FMath::Max(GateOpenDuration, 0.1f), 0.0, 1.0);
	UStaticMeshComponent* Gate = bOpeningGroupGate ? GroupReinforcementGate.Get() : ReinforcementGate.Get();
	const FVector& ClosedLocation = bOpeningGroupGate ? GroupGateClosedLocation : GateClosedLocation;
	Gate->SetRelativeLocation(
	    ClosedLocation + FVector(0, 0, GateLiftHeight * FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f)));
	if (Alpha < 1.0f)
	{
		return;
	}

	if (bOpeningGroupGate)
	{
		if (SecondPairReleaseAt < 0)
		{
			Gate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ReleaseStagedReinforcements(GroupPairSize);
			SecondPairReleaseAt = Now + GroupPairInterval;
		}
		if (Now < SecondPairReleaseAt && !StagedReinforcements.IsEmpty())
		{
			return;
		}
		ReleaseStagedReinforcements(StagedReinforcements.Num());
		SecondPairReleaseAt = -1;
	}
	GateOpenStartedAt = -1;
	SetActorTickEnabled(false);
	UpdateGates();
	CheckCleared();
}

void ADBCombatEncounter::PlayerDied()
{
	FinishBossIntroduction(false);
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
	const bool bReinforcementActive =
	    State == EDBEncounterState::ParryReinforcement || State == EDBEncounterState::Reinforcements;
	SetGate(EntranceGate,
	    State == EDBEncounterState::Active || State == EDBEncounterState::AirReinforcement || bReinforcementActive);
	const bool bCleared = State == EDBEncounterState::Cleared || State == EDBEncounterState::Completed;
	SetGate(ExitGate, !bCleared);
	const bool bHasGroupGate = GroupReinforcementGate->GetStaticMesh() != nullptr;
	const bool bGroupActive = State == EDBEncounterState::Reinforcements && bHasGroupGate;
	const bool bBossPhase = State == EDBEncounterState::BossActive || State == EDBEncounterState::BossIntroduction;
	SetGate(ReinforcementGate, !bCleared && (!bReinforcementActive || (GateOpenStartedAt >= 0 && !bGroupActive)));
	SetGate(
	    GroupReinforcementGate, bHasGroupGate && !bCleared && !bBossPhase && (!bGroupActive || GateOpenStartedAt >= 0));
	SetGate(BossGate, bBossPhase);
	if (IsValid(BossCheckpointIndicator))
	{
		BossCheckpointIndicator->SetActorHiddenInGame(State != EDBEncounterState::Cleared);
	}
	if (IsValid(FinishArea))
	{
		FinishArea->SetActorEnableCollision(State == EDBEncounterState::Cleared);
	}
}

void ADBCombatEncounter::EndPlay(const EEndPlayReason::Type Reason)
{
	FinishBossIntroduction(false);
	if (IsValid(Boss))
	{
		Boss->FindComponentByClass<UDBHealthComponent>()->OnDeath.RemoveAll(this);
	}
	EntryTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::Entered);
	if (IsValid(FinishArea))
	{
		FinishArea->OnActorBeginOverlap.RemoveDynamic(this, &ThisClass::FinishEntered);
	}
	if (EncounterPlayer.IsValid())
	{
		EncounterPlayer->FindComponentByClass<UDBHealthComponent>()->OnDeath.RemoveAll(this);
	}
	for (ADBEnemyCharacter* Enemy : Enemies)
	{
		if (IsValid(Enemy))
		{
			Enemy->FindComponentByClass<UDBHealthComponent>()->OnDeath.RemoveAll(this);
			Enemy->FindComponentByClass<UDBCombatComponent>()->OnTutorialParryContact.Unbind();
			Enemy->OnDestroyed.RemoveDynamic(this, &ThisClass::EnemyDestroyed);
		}
	}
	Super::EndPlay(Reason);
}
