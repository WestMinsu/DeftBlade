#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DBGameMode.generated.h"

UCLASS()
class DEFTBLADE_API ADBGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	static constexpr TCHAR BossCheckpointOption[] = TEXT("DBBossCheckpointScore");
	static constexpr TCHAR TitleMapPath[] = TEXT("/Game/DeftBlade/Maps/L_Title");

	static bool IsTitleMap(const UObject* WorldContext);
	virtual void RestartPlayer(AController* NewPlayer) override;
};
