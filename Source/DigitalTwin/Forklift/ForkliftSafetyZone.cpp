#include "Forklift/ForkliftSafetyZone.h"

#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"

AForkliftSafetyZone::AForkliftSafetyZone()
{
	PrimaryActorTick.bCanEverTick = true;
	ZoneVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SafetyZoneVolume"));
	SetRootComponent(ZoneVolume);
	ZoneVolume->SetBoxExtent(FVector(300.f, 300.f, 200.f));
	ZoneVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneVolume->SetGenerateOverlapEvents(false);
}

void AForkliftSafetyZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (ZoneVolume)
		ZoneVolume->ShapeColor = ZoneType == EForkliftSafetyZoneType::NoEntry ? FColor(255, 50, 45) : FColor(30, 190, 255);
}

void AForkliftSafetyZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDrawDebugZone || !ZoneVolume || !GetWorld()) return;
	const FColor Color = ZoneType == EForkliftSafetyZoneType::NoEntry ? FColor(255, 50, 45) : FColor(30, 190, 255);
	DrawDebugBox(GetWorld(), ZoneVolume->GetComponentLocation(), ZoneVolume->GetScaledBoxExtent(),
		ZoneVolume->GetComponentQuat(), Color, false, 0.f, 0, 2.f);
}

bool AForkliftSafetyZone::ContainsPointExpanded(const FVector& WorldPoint, float ExpansionCm) const
{
	if (!ZoneVolume) return false;
	return ZoneVolume->Bounds.GetBox().ExpandBy(FMath::Max(0.f, ExpansionCm)).IsInsideOrOn(WorldPoint);
}
