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

	virtual void RestartPlayer(AController* NewPlayer) override;
};
