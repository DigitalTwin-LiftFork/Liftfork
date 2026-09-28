// Copyright Epic Games, Inc. All Rights Reserved.

#include "DigitalTwinGameMode.h"
#include "Forklift/AutomatedForklift.h"
#include "Forklift/ForkliftPlayerController.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"

ADigitalTwinGameMode::ADigitalTwinGameMode()
{
	// Prevent the top-down template pawn/controller from being created for Map1.
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AForkliftPlayerController::StaticClass();
}

void ADigitalTwinGameMode::BeginPlay()
{
	Super::BeginPlay();
	FindOrSpawnForklift();
	PossessForklift(GetWorld()->GetFirstPlayerController());
}

void ADigitalTwinGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	PossessForklift(NewPlayer);
}

AAutomatedForklift* ADigitalTwinGameMode::FindOrSpawnForklift()
{
	if (IsValid(ActiveForklift))
	{
		return ActiveForklift;
	}

	// A forklift authored in Map1 wins over runtime creation.
	for (TActorIterator<AAutomatedForklift> It(GetWorld()); It; ++It)
	{
		ActiveForklift = *It;
		return ActiveForklift;
	}

	FVector SpawnLocation(0.f, 0.f, 55.f); // Body collision is centred on this location and clears a floor at Z=0.
	FRotator SpawnRotation = FRotator::ZeroRotator;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		SpawnLocation = It->GetActorLocation() + FVector(0.f, 0.f, 55.f);
		SpawnRotation = It->GetActorRotation();
		break;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ActiveForklift = GetWorld()->SpawnActor<AAutomatedForklift>(AAutomatedForklift::StaticClass(), SpawnLocation, SpawnRotation, SpawnParameters);
	return ActiveForklift;
}

void ADigitalTwinGameMode::PossessForklift(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}

	if (AAutomatedForklift* Forklift = FindOrSpawnForklift())
	{
		if (PlayerController->GetPawn() != Forklift)
		{
			PlayerController->Possess(Forklift);
		}
	}
}
