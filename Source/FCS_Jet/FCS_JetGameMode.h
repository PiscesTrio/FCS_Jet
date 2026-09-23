// FCS_Jet - default game mode

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FCS_JetGameMode.generated.h"

/**
 * Spawns the flyable ship at the PlayerStart and nothing else.
 *
 * Selected through GlobalDefaultGameMode in DefaultEngine.ini so that no Blueprint
 * wrapper is needed anywhere in the project.
 */
UCLASS()
class FCS_JET_API AFCS_JetGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFCS_JetGameMode();
};
