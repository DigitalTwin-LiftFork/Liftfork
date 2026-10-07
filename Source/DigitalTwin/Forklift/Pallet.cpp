#include "Forklift/Pallet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

APallet::APallet()
{
	PrimaryActorTick.bCanEverTick = false;
	CollisionBody = CreateDefaultSubobject<UBoxComponent>(TEXT("PalletCollision"));
	SetRootComponent(CollisionBody);
	CollisionBody->SetBoxExtent(FVector(55.f, 10.f, 1.f));
	CollisionBody->SetCollisionProfileName(TEXT("PhysicsActor"));
	CollisionBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBody->SetMobility(EComponentMobility::Movable);
	CollisionShapes.Add(CollisionBody);
	AddCollisionShape(TEXT("LeftRunnerCollision"), FVector(0.f, -47.5f, 0.f), FVector(55.f, 7.5f, 1.f));
	AddCollisionShape(TEXT("RightRunnerCollision"), FVector(0.f, 47.5f, 0.f), FVector(55.f, 7.5f, 1.f));
	AddCollisionShape(TEXT("DeckSlat01Collision"), FVector(-47.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	AddCollisionShape(TEXT("DeckSlat02Collision"), FVector(-31.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	AddCollisionShape(TEXT("DeckSlat03Collision"), FVector(-15.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	AddCollisionShape(TEXT("DeckSlat04Collision"), FVector(0.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	AddCollisionShape(TEXT("DeckSlat05Collision"), FVector(15.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	AddCollisionShape(TEXT("DeckSlat06Collision"), FVector(31.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	AddCollisionShape(TEXT("DeckSlat07Collision"), FVector(47.f, 0.f, 13.f), FVector(7.5f, 55.f, 1.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		// Exact 110 x 110 x 15 cm pallet: 2 cm top/bottom, 11 cm fork openings.
		AddBoard(TEXT("DeckSlat01"), FVector(-47.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("DeckSlat02"), FVector(-31.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("DeckSlat03"), FVector(-15.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("DeckSlat04"), FVector(0.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("DeckSlat05"), FVector(15.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("DeckSlat06"), FVector(31.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("DeckSlat07"), FVector(47.f, 0.f, 13.f), FVector(0.15f, 1.1f, 0.02f));
		AddBoard(TEXT("RunnerLeft"), FVector(0.f, -47.5f, 0.f), FVector(1.1f, 0.15f, 0.02f));
		AddBoard(TEXT("RunnerCenter"), FVector(0.f, 0.f, 0.f), FVector(1.1f, 0.2f, 0.02f));
		AddBoard(TEXT("RunnerRight"), FVector(0.f, 47.5f, 0.f), FVector(1.1f, 0.15f, 0.02f));
	}
}

void APallet::BeginPlay()
{
	Super::BeginPlay();
	SetCarriedState(false);
}

void APallet::SetCarriedState(bool bIsCarried)
{
	if (!CollisionBody) return;
	if (bIsCarried)
	{
		CollisionBody->SetSimulatePhysics(false);
		for (UBoxComponent* Shape : CollisionShapes) Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}
	CollisionBody->SetEnableGravity(true);
	CollisionBody->SetSimulatePhysics(true);
	for (UBoxComponent* Shape : CollisionShapes) Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

UStaticMeshComponent* APallet::AddBoard(const TCHAR* Name, const FVector& Location, const FVector& Scale)
{
	UStaticMeshComponent* Board = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Board->SetupAttachment(CollisionBody);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Board->SetStaticMesh(Cube.Object);
	Board->SetRelativeLocation(Location);
	Board->SetRelativeScale3D(Scale);
	Board->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return Board;
}

UBoxComponent* APallet::AddCollisionShape(const TCHAR* Name, const FVector& Location, const FVector& HalfExtent)
{
	UBoxComponent* Shape = CreateDefaultSubobject<UBoxComponent>(Name);
	Shape->SetupAttachment(CollisionBody);
	Shape->SetRelativeLocation(Location);
	Shape->SetBoxExtent(HalfExtent);
	Shape->SetCollisionProfileName(TEXT("PhysicsActor"));
	Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Shape->SetMobility(EComponentMobility::Movable);
	CollisionShapes.Add(Shape);
	return Shape;
}
