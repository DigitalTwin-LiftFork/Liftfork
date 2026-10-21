#include "Forklift/WoodPallet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AWoodPallet::AWoodPallet()
{
	PhysicsRoot = CreateDefaultSubobject<UBoxComponent>(TEXT("WoodPalletPhysicsRoot"));
	SetRootComponent(PhysicsRoot);
	PhysicsRoot->SetBoxExtent(FVector(2.f)); // Host body; the following simple shapes preserve the two openings.
	PhysicsRoot->SetCollisionProfileName(TEXT("PhysicsActor"));
	PhysicsRoot->SetSimulatePhysics(true); PhysicsRoot->SetEnableGravity(true); PhysicsRoot->SetNotifyRigidBodyCollision(true);
	PhysicsRoot->OnComponentHit.AddDynamic(this, &AWoodPallet::HandleHit);
	CollisionShapes.Add(PhysicsRoot);
	// Assumptions: 2cm top boards, 1.5cm lower runners, 10cm opening.  The
	// three rails are sized to leave two 36cm clear openings centred at Y +/-30.
	constexpr float TopThickness = 2.f, RunnerThickness = 1.5f, SupportHeight = 10.f;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Y = -50.f + Index * (10.f + 30.f / 7.f);
		AddWoodBox(*FString::Printf(TEXT("TopDeckBoard_%d"), Index), FVector(0.f, Y, HeightCm() - TopThickness * .5f), FVector(LengthCm(), 10.f, TopThickness));
		AddCollisionBox(*FString::Printf(TEXT("TopDeckCollision_%d"), Index), FVector(0.f, Y, HeightCm() - TopThickness * .5f), FVector(LengthCm() * .5f, 5.f, TopThickness * .5f));
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Y = Index == 0 ? -51.5f : (Index == 1 ? 0.f : 51.5f);
		const float RailWidth = Index == 1 ? 24.f : 7.f;
		AddWoodBox(*FString::Printf(TEXT("LowerRunner_%d"), Index), FVector(0.f, Y, RunnerThickness * .5f), FVector(LengthCm(), RailWidth, RunnerThickness));
		AddCollisionBox(*FString::Printf(TEXT("LowerRunnerCollision_%d"), Index), FVector(0.f, Y, RunnerThickness * .5f), FVector(LengthCm() * .5f, RailWidth * .5f, RunnerThickness * .5f));
		AddWoodBox(*FString::Printf(TEXT("SupportRail_%d"), Index), FVector(0.f, Y, RunnerThickness + SupportHeight * .5f), FVector(LengthCm(), RailWidth, SupportHeight));
		AddCollisionBox(*FString::Printf(TEXT("SupportRailCollision_%d"), Index), FVector(0.f, Y, RunnerThickness + SupportHeight * .5f), FVector(LengthCm() * .5f, RailWidth * .5f, SupportHeight * .5f));
	}
}

UStaticMeshComponent* AWoodPallet::AddWoodBox(const TCHAR* Name, const FVector& Location, const FVector& FullSize)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name); Part->SetupAttachment(PhysicsRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Part->SetStaticMesh(Cube.Object);
	Part->SetRelativeLocation(Location); Part->SetRelativeScale3D(FullSize / 100.f); Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Deliberately high-saturation red: this is a simulation/vision aid, not a
	// claim about a real wood-pallet finish.  It is rendered by the same sensor.
	Part->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(.78f, .035f, .025f));
	VisualParts.Add(Part); return Part;
}
UBoxComponent* AWoodPallet::AddCollisionBox(const TCHAR* Name, const FVector& Location, const FVector& HalfExtent)
{
	UBoxComponent* Part = CreateDefaultSubobject<UBoxComponent>(Name); Part->SetupAttachment(PhysicsRoot); Part->SetRelativeLocation(Location); Part->SetBoxExtent(HalfExtent);
	Part->SetCollisionProfileName(TEXT("PhysicsActor"));
	Part->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Part->SetNotifyRigidBodyCollision(true);
	Part->OnComponentHit.AddDynamic(this, &AWoodPallet::HandleHit);
	CollisionShapes.Add(Part);
	return Part;
}
void AWoodPallet::BeginPlay() { Super::BeginPlay(); FinalizePlacement(); }
void AWoodPallet::SetPlacementPreview(bool bPreview)
{
	bPlacementPreview = bPreview; PhysicsRoot->SetSimulatePhysics(false); PhysicsRoot->SetEnableGravity(false);
	for (UBoxComponent* Shape : CollisionShapes) Shape->SetCollisionEnabled(bPreview ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	for (UStaticMeshComponent* Part : VisualParts) Part->SetVectorParameterValueOnMaterials(TEXT("Color"), bPreview ? FVector(.25f, .8f, .3f) : FVector(.78f, .035f, .025f));
}
void AWoodPallet::FinalizePlacement()
{
	bPlacementPreview = false; for (UBoxComponent* Shape : CollisionShapes) Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysicsRoot->SetEnableGravity(true); PhysicsRoot->SetSimulatePhysics(true); PhysicsRoot->SetPhysicsLinearVelocity(FVector::ZeroVector); PhysicsRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	for (UStaticMeshComponent* Part : VisualParts) Part->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(.78f, .035f, .025f));
}
void AWoodPallet::HandleHit(UPrimitiveComponent*, AActor*, UPrimitiveComponent* Other, FVector, const FHitResult& Hit) { LastHit = Hit; LastHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; LastHitOther = Other; }
bool AWoodPallet::GetRecentForkContact(float MaxAge, UPrimitiveComponent*& OutOther) const { OutOther = nullptr; if (!GetWorld() || GetWorld()->GetTimeSeconds() - LastHitTime > MaxAge) return false; OutOther = LastHitOther.Get(); return OutOther != nullptr; }
