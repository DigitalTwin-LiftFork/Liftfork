#include "Forklift/Pallet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"

APallet::APallet()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		// 120 x 100 cm footprint: the two outer runners leave a clear lane for
		// the forklift tines at Y = +/-30 cm; the middle runner separates them.
		AddBoard(TEXT("DeckSlat01"), FVector(-54.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat02"), FVector(-36.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat03"), FVector(-18.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat04"), FVector(0.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat05"), FVector(18.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat06"), FVector(36.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat07"), FVector(54.f, 0.f, 12.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("RunnerLeft"), FVector(0.f, -40.f, 5.f), FVector(1.2f, 0.04f, 0.05f));
		AddBoard(TEXT("RunnerCenter"), FVector(0.f, 0.f, 5.f), FVector(1.2f, 0.04f, 0.05f));
		AddBoard(TEXT("RunnerRight"), FVector(0.f, 40.f, 5.f), FVector(1.2f, 0.04f, 0.05f));
	}
}

UStaticMeshComponent* APallet::AddBoard(const TCHAR* Name, const FVector& Location, const FVector& Scale)
{
	UStaticMeshComponent* Board = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Board->SetupAttachment(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Board->SetStaticMesh(Cube.Object);
	Board->SetRelativeLocation(Location);
	Board->SetRelativeScale3D(Scale);
	Board->SetCollisionProfileName(TEXT("BlockAll"));
	return Board;
}
