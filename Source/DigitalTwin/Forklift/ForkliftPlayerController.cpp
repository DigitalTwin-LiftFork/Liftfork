#include "Forklift/ForkliftPlayerController.h"

AForkliftPlayerController::AForkliftPlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void AForkliftPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// The pawn's UCameraComponent becomes the active view target; no template camera can take control.
	SetViewTarget(InPawn);
}
