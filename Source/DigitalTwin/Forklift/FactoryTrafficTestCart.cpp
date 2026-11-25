#include "Forklift/FactoryTrafficTestCart.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AFactoryTrafficTestCart::AFactoryTrafficTestCart()
{
	PrimaryActorTick.bCanEverTick = true;
	CartCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("CartCollision"));
	SetRootComponent(CartCollision);
	CartCollision->SetBoxExtent(FVector(120.f, 65.f, 100.f));
	CartCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CartCollision->SetCollisionObjectType(ECC_WorldDynamic);
	CartCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	CartCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CartCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CartCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	CartCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CartCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	CartVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CartVisual"));
	CartVisual->SetupAttachment(CartCollision);
	CartVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		CartVisual->SetStaticMesh(CubeMesh.Object);
		CartVisual->SetRelativeScale3D(FVector(2.4f, 1.3f, 2.f));
	}
}

void AFactoryTrafficTestCart::BeginPlay()
{
	Super::BeginPlay();
	StartLocation = GetActorLocation();
	EndLocation = StartLocation + GetActorRightVector() * TravelDistanceCm;
	UE_LOG(LogTemp, Log, TEXT("[FACTORY_TRAFFIC_TEST] crossing cart started: %s -> %s speed=%.0f cm/s"),
		*StartLocation.ToString(), *EndLocation.ToString(), TravelSpeedCmPerSec);
}

void AFactoryTrafficTestCart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FVector Target = bMovingToEnd ? EndLocation : StartLocation;
	const FVector Next = FMath::VInterpConstantTo(GetActorLocation(), Target, DeltaSeconds, TravelSpeedCmPerSec);
	FHitResult Hit;
	SetActorLocation(Next, true, &Hit, ETeleportType::None);
	if (Hit.bBlockingHit || FVector::DistSquared(GetActorLocation(), Target) < FMath::Square(5.f))
	{
		bMovingToEnd = !bMovingToEnd;
		if (Hit.bBlockingHit)
			UE_LOG(LogTemp, Verbose, TEXT("[FACTORY_TRAFFIC_TEST] cart yielded at %s"), *GetNameSafe(Hit.GetActor()));
	}
}
