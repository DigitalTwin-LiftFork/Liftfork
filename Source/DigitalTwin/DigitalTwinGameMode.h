// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DigitalTwinGameMode.generated.h"

/**
 *  Simple Game Mode for a top-down perspective game
 *  Sets the default gameplay framework classes
 *  Check the Blueprint derived class for the set values
 */
UCLASS()
class ADigitalTwinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	ADigitalTwinGameMode();
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

private:
	TObjectPtr<class AAutomatedForklift> ActiveForklift;
	AAutomatedForklift* FindOrSpawnForklift();
	void PossessForklift(APlayerController* PlayerController);
};



