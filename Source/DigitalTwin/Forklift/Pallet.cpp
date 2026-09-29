#include "Forklift/Pallet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

APallet::APallet()
{
	PrimaryActorTick.bCanEverTick = false;
	CollisionBody = CreateDefaultSubobject<UBoxComponent>(TEXT("PalletCollision"));
	SetRootComponent(CollisionBody);
	CollisionBody->SetBoxExtent(FVector(60.f, 4.f, 5.f));
	CollisionBody->SetCollisionProfileName(TEXT("PhysicsActor"));
	CollisionBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBody->SetMobility(EComponentMobility::Movable);
	CollisionShapes.Add(CollisionBody);
	AddCollisionShape(TEXT("LeftRunnerCollision"), FVector(0.f, -44.f, 0.f), FVector(60.f, 3.f, 5.f));
	AddCollisionShape(TEXT("RightRunnerCollision"), FVector(0.f, 44.f, 0.f), FVector(60.f, 3.f, 5.f));
	AddCollisionShape(TEXT("DeckSlat01Collision"), FVector(-54.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	AddCollisionShape(TEXT("DeckSlat02Collision"), FVector(-36.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	AddCollisionShape(TEXT("DeckSlat03Collision"), FVector(-18.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	AddCollisionShape(TEXT("DeckSlat04Collision"), FVector(0.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	AddCollisionShape(TEXT("DeckSlat05Collision"), FVector(18.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	AddCollisionShape(TEXT("DeckSlat06Collision"), FVector(36.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	AddCollisionShape(TEXT("DeckSlat07Collision"), FVector(54.f, 0.f, 7.f), FVector(5.f, 50.f, 2.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		// 120 x 100 cm footprint. The three runners leave two 38 cm fork lanes.
		AddBoard(TEXT("DeckSlat01"), FVector(-54.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat02"), FVector(-36.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat03"), FVector(-18.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat04"), FVector(0.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat05"), FVector(18.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat06"), FVector(36.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("DeckSlat07"), FVector(54.f, 0.f, 7.f), FVector(0.1f, 1.f, 0.04f));
		AddBoard(TEXT("RunnerLeft"), FVector(0.f, -44.f, 0.f), FVector(1.2f, 0.06f, 0.1f));
		AddBoard(TEXT("RunnerCenter"), FVector(0.f, 0.f, 0.f), FVector(1.2f, 0.08f, 0.1f));
		AddBoard(TEXT("RunnerRight"), FVector(0.f, 44.f, 0.f), FVector(1.2f, 0.06f, 0.1f));
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
