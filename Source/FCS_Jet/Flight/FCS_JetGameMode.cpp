// FCS_Jet - default game mode

#include "Flight/FCS_JetGameMode.h"

#include "Flight/FCS_JetPawn.h"

AFCS_JetGameMode::AFCS_JetGameMode()
{
	DefaultPawnClass = AFCS_JetPawn::StaticClass();
}
