#include "Forklift/Pallet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

APallet::APallet()
{
	PrimaryActorTick.bCanEverTick = false;
	// Actor origin is the centre of the 20 mm lower deck.  A spawned actor at
	// Z=1 cm therefore rests on the floor while its full height is 15 cm.
	CollisionBody = CreateDefaultSubobject<UBoxComponent>(TEXT("PalletCollision"));
	SetRootComponent(CollisionBody);
	CollisionBody->SetBoxExtent(FVector(DepthCm * .5f, WidthCm * .5f, DeckThicknessCm * .5f));
	CollisionBody->SetCollisionProfileName(TEXT("PhysicsActor"));
	CollisionBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBody->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionBody->SetNotifyRigidBodyCollision(true);
	CollisionBody->OnComponentHit.AddDynamic(this, &APallet::HandlePalletHit);
	CollisionBody->SetMobility(EComponentMobility::Movable);
	CollisionShapes.Add(CollisionBody);
	// The lower deck top is Z=+1 and the upper deck underside is Z=+12,
	// leaving the specified 11 cm insertion height.
	AddCollisionShape(TEXT("UpperDeckCollision"), FVector(0.f, 0.f, HeightCm - DeckThicknessCm), FVector(DepthCm * .5f, WidthCm * .5f, DeckThicknessCm * .5f));
	AddCollisionShape(TEXT("LeftSupportCollision"), FVector(0.f, -47.5f, 6.5f), FVector(DepthCm * .5f, 7.5f, ForkOpeningHeightCm * .5f));
	AddCollisionShape(TEXT("CenterSupportCollision"), FVector(0.f, 0.f, 6.5f), FVector(DepthCm * .5f, 10.f, ForkOpeningHeightCm * .5f));
	AddCollisionShape(TEXT("RightSupportCollision"), FVector(0.f, 47.5f, 6.5f), FVector(DepthCm * .5f, 7.5f, ForkOpeningHeightCm * .5f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		AddBoard(TEXT("LowerDeck"), FVector::ZeroVector, FVector(1.1f, 1.1f, .02f));
		AddBoard(TEXT("UpperDeck"), FVector(0.f, 0.f, HeightCm - DeckThicknessCm), FVector(1.1f, 1.1f, .02f));
		AddBoard(TEXT("LeftSupport"), FVector(0.f, -47.5f, 6.5f), FVector(1.1f, .15f, .11f));
		AddBoard(TEXT("CenterSupport"), FVector(0.f, 0.f, 6.5f), FVector(1.1f, .20f, .11f));
		AddBoard(TEXT("RightSupport"), FVector(0.f, 47.5f, 6.5f), FVector(1.1f, .15f, .11f));
	}
}

void APallet::HandlePalletHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	LastPhysicsHit = Hit;
	LastPhysicsHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	LastPhysicsOther = OtherComponent;
}

bool APallet::GetRecentPhysicsContact(float MaxAgeSeconds, FHitResult& OutHit, UPrimitiveComponent*& OutOther) const
{
	if (!GetWorld() || GetWorld()->GetTimeSeconds() - LastPhysicsHitTime > MaxAgeSeconds) return false;
	OutHit = LastPhysicsHit;
	OutOther = LastPhysicsOther.Get();
	return OutOther != nullptr;
}

bool APallet::CanAcceptForkTines(const UBoxComponent* LeftTine, const UBoxComponent* RightTine) const
{
	if (!LeftTine || !RightTine) return false;
	const FTransform PalletTransform = GetActorTransform();
	const float OpeningHalfWidth = ForkOpeningWidthCm * .5f;
	const float GapMinZ = DeckThicknessCm * .5f;
	const float GapMaxZ = HeightCm - DeckThicknessCm * 1.5f;
	const auto IsTineInsideOpening = [&](const UBoxComponent* Tine, float OpeningCenterY)
	{
		const FVector LocalCenter = PalletTransform.InverseTransformPosition(Tine->GetComponentLocation());
		const FVector Extent = Tine->GetScaledBoxExtent();
		const FVector LocalTip = PalletTransform.InverseTransformPosition(Tine->GetComponentTransform().TransformPosition(FVector(Extent.X, 0.f, 0.f)));
		return FMath::Abs(LocalCenter.Y - OpeningCenterY) + Extent.Y <= OpeningHalfWidth
			&& LocalCenter.Z - Extent.Z >= GapMinZ
			&& LocalCenter.Z + Extent.Z <= GapMaxZ
			&& LocalTip.X >= -DepthCm * .5f && LocalTip.X <= DepthCm * .5f;
	};

	return IsTineInsideOpening(LeftTine, -ForkOpeningCenterOffsetCm)
		&& IsTineInsideOpening(RightTine, ForkOpeningCenterOffsetCm);
}

bool APallet::CanSupportFromForks(const UBoxComponent* LeftTine, const UBoxComponent* RightTine, float MinimumInsertionDepthCm, FString& OutReason) const
{
	OutReason.Reset();
	if (!LeftTine || !RightTine)
	{
		OutReason = TEXT("fork collision components are missing");
		return false;
	}
	if (!CanAcceptForkTines(LeftTine, RightTine))
	{
		OutReason = TEXT("both complete fork cross-sections are not inside the specified openings");
		return false;
	}
	const FTransform PalletTransform = GetActorTransform();
	const float RequiredTipX = -DepthCm * .5f + FMath::Max(0.f, MinimumInsertionDepthCm);
	for (const UBoxComponent* Tine : {LeftTine, RightTine})
	{
		const FVector Tip = PalletTransform.InverseTransformPosition(Tine->GetComponentTransform().TransformPosition(FVector(Tine->GetScaledBoxExtent().X, 0.f, 0.f)));
		if (Tip.X < RequiredTipX)
		{
			OutReason = FString::Printf(TEXT("fork insertion %.1fcm is below required %.1fcm"), Tip.X + DepthCm * .5f, MinimumInsertionDepthCm);
			return false;
		}
	}
	return true;
}

float APallet::GetForkSupportSurfaceWorldZ() const
{
	// The fork supports the underside of the upper deck (local Z=12 cm).
	return GetActorTransform().TransformPosition(FVector(0.f, 0.f, HeightCm - DeckThicknessCm * 1.5f)).Z;
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
		CollisionBody->SetEnableGravity(false);
		// Keep query shapes alive: the forklift explicitly ignores this attached
		// actor for self-collision, then sweeps it against external obstacles.
		for (UBoxComponent* Shape : CollisionShapes) Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
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
	Shape->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Shape->SetMobility(EComponentMobility::Movable);
	CollisionShapes.Add(Shape);
	return Shape;
}
