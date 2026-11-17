#include "Forklift/Pallet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
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
	CollisionBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionBody->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
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
	CargoVisual = AddBoard(TEXT("CargoVisual"), FVector(0,0,54), FVector(1,1,.8));
	CargoCollision = AddCollisionShape(TEXT("CargoCollision"), FVector(0,0,54), FVector(50,50,40));
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
	const FTransform PalletTransform = GetActorTransform();
	const float PalletMinX = -DepthCm * .5f;
	const float PalletMaxX = DepthCm * .5f;
	const float PalletMinY = -WidthCm * .5f;
	const float PalletMaxY = WidthCm * .5f;
	const float RequiredInsertion = FMath::Max(1.f, MinimumInsertionDepthCm);
	TArray<FVector> TineCenters;
	for (const UBoxComponent* Tine : {LeftTine, RightTine})
	{
		const FVector Extent = Tine->GetScaledBoxExtent();
		const FTransform TineTransform = Tine->GetComponentTransform();
		FBox LocalBounds(ForceInit);
		for (int32 XSign : {-1, 1})
			for (int32 YSign : {-1, 1})
				for (int32 ZSign : {-1, 1})
					LocalBounds += PalletTransform.InverseTransformPosition(TineTransform.TransformPosition(FVector(Extent.X * XSign, Extent.Y * YSign, Extent.Z * ZSign)));

		const float OverlapX = FMath::Min(LocalBounds.Max.X, PalletMaxX) - FMath::Max(LocalBounds.Min.X, PalletMinX);
		const float OverlapY = FMath::Min(LocalBounds.Max.Y, PalletMaxY) - FMath::Max(LocalBounds.Min.Y, PalletMinY);
		if (OverlapX < RequiredInsertion || OverlapY < 2.f)
		{
			OutReason = FString::Printf(TEXT("fork overlap %.1f x %.1fcm is below support minimum %.1f x 2.0cm"), OverlapX, OverlapY, RequiredInsertion);
			return false;
		}
		TineCenters.Add(LocalBounds.GetCenter());
	}
	// Keep both tines spread across the load. This allows a small lateral shift
	// and forks extending past the rear edge while rejecting a one-sided contact.
	if (FMath::Abs(TineCenters[0].Y - TineCenters[1].Y) < 30.f || FMath::Abs((TineCenters[0].Y + TineCenters[1].Y) * .5f) > WidthCm * .35f)
	{
		OutReason = TEXT("forks do not span the pallet support area");
		return false;
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
	// Establish the optional cargo collision before applying the shared Chaos
	// physical material.  Otherwise the cargo keeps the engine-default friction.
	CollisionShapes.Remove(CargoCollision);
	if (bHasCargo) CollisionShapes.AddUnique(CargoCollision);
	CargoVisual->SetVisibility(bHasCargo);
	CargoCollision->SetCollisionEnabled(bHasCargo ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this, TEXT("PalletChaosMaterial"));
	RuntimePhysicalMaterial->Friction = SurfaceFriction;
	RuntimePhysicalMaterial->Restitution = .02f;
	RuntimePhysicalMaterial->bOverrideFrictionCombineMode = true;
	RuntimePhysicalMaterial->FrictionCombineMode = EFrictionCombineMode::Multiply;
	for (UBoxComponent* Shape : CollisionShapes) Shape->SetPhysMaterialOverride(RuntimePhysicalMaterial);
	CollisionBody->SetMassOverrideInKg(NAME_None, GetConfiguredMassKg(), true);
	CollisionBody->SetLinearDamping(1.25f);
	CollisionBody->SetAngularDamping(3.5f);
	SetCarriedState(false);
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_PALLET_PHYSICS] pallet=%s sim=%d gravity=%d collision=%d root=%s"), *GetName(), CollisionBody->IsSimulatingPhysics(), CollisionBody->IsGravityEnabled(), static_cast<int32>(CollisionBody->GetCollisionEnabled()), *CollisionBody->GetComponentLocation().ToString());
}

FString APallet::GetRecentCollisionStatus() const
{
	if (!GetWorld() || GetWorld()->GetTimeSeconds() - LastPhysicsHitTime > .5f) return TEXT("None");
	return FString::Printf(TEXT("%s"), *GetNameSafe(LastPhysicsOther.Get()));
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
	Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Shape->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Shape->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Shape->SetMobility(EComponentMobility::Movable);
	CollisionShapes.Add(Shape);
	return Shape;
}

void APallet::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CargoVisual->SetVisibility(bHasCargo);
	CargoCollision->SetCollisionEnabled(bHasCargo ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	CollisionShapes.Remove(CargoCollision);
	if (bHasCargo) CollisionShapes.AddUnique(CargoCollision);
	// Keep the authored Chaos mass in sync with the optional box before PIE;
	// BeginPlay also applies the runtime material and damping.
	CollisionBody->SetMassOverrideInKg(NAME_None, GetConfiguredMassKg(), true);
}
