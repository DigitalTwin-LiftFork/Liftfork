// Dedicated controller for the C++ forklift simulation. No template click-to-move bindings.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ForkliftPlayerController.generated.h"

UCLASS()
class DIGITALTWIN_API AForkliftPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AForkliftPlayerController();
	virtual void OnPossess(APawn* InPawn) override;
};
