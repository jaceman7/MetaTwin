#include "TwinGameMode.h"

#include "GameFramework/SpectatorPawn.h"
#include "TwinPlayerController.h"

ATwinGameMode::ATwinGameMode()
{
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	PlayerControllerClass = ATwinPlayerController::StaticClass();
}
