#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ForkliftSensorHUD.generated.h"

/** Draws the one sensor render target; it does not create or capture another camera. */
UCLASS()
class DIGITALTWIN_API AForkliftSensorHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
