// FCS_Jet - default game mode

#include "FCS_JetGameMode.h"

#include "FCS_JetPawn.h"

AFCS_JetGameMode::AFCS_JetGameMode()
{
	DefaultPawnClass = AFCS_JetPawn::StaticClass();
}
