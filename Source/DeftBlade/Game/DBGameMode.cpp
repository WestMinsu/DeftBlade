#include "Game/DBGameMode.h"

#include "Character/DBPlayerCharacter.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Game/DBCombatEncounter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

bool ADBGameMode::IsTitleMap(const UObject* WorldContext)
{
	return UGameplayStatics::GetCurrentLevelName(WorldContext) == FPackageName::GetShortName(TitleMapPath);
}

void ADBGameMode::RestartPlayer(AController* NewPlayer)
{
	if (IsTitleMap(this))
	{
		return;
	}

	const int32 CheckpointScore = UGameplayStatics::GetIntOption(OptionsString, BossCheckpointOption, INDEX_NONE);
	if (NewPlayer && !NewPlayer->GetPawn() && CheckpointScore >= 0)
	{
		for (TActorIterator<ADBCombatEncounter> It(GetWorld()); It; ++It)
		{
			if (ATargetPoint* Start = It->GetBossCheckpoint())
			{
				RestartPlayerAtPlayerStart(NewPlayer, Start);
				if (auto* Player = Cast<ADBPlayerCharacter>(NewPlayer->GetPawn()))
				{
					It->RestoreBossCheckpoint(Player, CheckpointScore);
					return;
				}
				break;
			}
		}
	}
	Super::RestartPlayer(NewPlayer);
}
