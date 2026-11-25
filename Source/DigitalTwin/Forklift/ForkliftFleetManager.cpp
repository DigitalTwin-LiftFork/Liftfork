#include "Forklift/ForkliftFleetManager.h"

#include "Forklift/AutomatedForklift.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"

AForkliftFleetManager::AForkliftFleetManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .5f;
}

void AForkliftFleetManager::RegisterForklift(AAutomatedForklift* Forklift)
{
	if (!IsValid(Forklift) || Forklifts.Contains(Forklift)) return;
	Forklifts.Add(Forklift);
	Forklift->SetFleetIdentity(NextFleetId++, this);
}

bool AForkliftFleetManager::IsSpawnAreaClear(const FVector& Location, const AAutomatedForklift* IgnoreForklift) const
{
	if (!GetWorld()) return false;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ForkliftFleetSpawn), false, IgnoreForklift);
	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Footprint = FCollisionShape::MakeBox(FVector(190.f, 125.f, 100.f));
	if (!GetWorld()->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, ObjectTypes, Footprint, Query)) return true;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AActor* Other = Overlap.GetActor();
		if (Other && Other != IgnoreForklift && !Other->IsA<AForkliftFleetManager>()) return false;
	}
	return true;
}

bool AForkliftFleetManager::DeployFleet(AAutomatedForklift* LeadForklift)
{
	if (!GetWorld() || !IsValid(LeadForklift)) return false;
	const FVector Origin = LeadForklift->GetActorLocation();
	const FRotator Rotation = LeadForklift->GetActorRotation();
	const FVector Right = LeadForklift->GetActorRightVector();
	const FVector Forward = LeadForklift->GetActorForwardVector();
	const FVector Offsets[] = {
		Right * 650.f, -Right * 650.f, Forward * 850.f, -Forward * 850.f,
		(Right + Forward).GetSafeNormal() * 1100.f, (Right - Forward).GetSafeNormal() * 1100.f,
		(-Right + Forward).GetSafeNormal() * 1100.f, (-Right - Forward).GetSafeNormal() * 1100.f
	};

	int32 SpawnedCount = 0;
	for (const FVector& Offset : Offsets)
	{
		if (Forklifts.Num() >= MaximumFleetSize) break;
		const FVector Candidate = Origin + Offset;
		if (!IsSpawnAreaClear(Candidate, LeadForklift)) continue;
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
		AAutomatedForklift* NewForklift = GetWorld()->SpawnActor<AAutomatedForklift>(
			AAutomatedForklift::StaticClass(), Candidate, Rotation, SpawnParams);
		if (!IsValid(NewForklift)) continue;
		RegisterForklift(NewForklift);
		++SpawnedCount;
	}
	if (SpawnedCount > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[FORKLIFT_FLEET] Deployed %d additional forklifts; fleet size %d."), SpawnedCount, Forklifts.Num());
	}
	return SpawnedCount > 0;
}

void AForkliftFleetManager::ToggleFleetOperation(AAutomatedForklift* RequestingForklift)
{
	if (!IsValid(RequestingForklift)) return;
	RegisterForklift(RequestingForklift);
	if (Forklifts.Num() < MaximumFleetSize) DeployFleet(RequestingForklift);

	bFleetMoving = !bFleetMoving;
	for (AAutomatedForklift* Forklift : Forklifts)
	{
		if (!IsValid(Forklift)) continue;
		if (bFleetMoving)
		{
			if (!Forklift->IsNavigationActive()) Forklift->ToggleNavigation();
		}
		else
		{
			Forklift->StopNavigation();
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[FORKLIFT_FLEET] Fleet %s; active vehicles %d."), bFleetMoving ? TEXT("started") : TEXT("stopped"), Forklifts.Num());
}

void AForkliftFleetManager::UpdateDeadlockHandling(float DeltaSeconds)
{
	for (int32 A = 0; A < Forklifts.Num(); ++A)
	{
		AAutomatedForklift* First = Forklifts[A];
		if (!IsValid(First) || !First->IsNavigationActive()) continue;
		for (int32 B = A + 1; B < Forklifts.Num(); ++B)
		{
			AAutomatedForklift* Second = Forklifts[B];
			if (!IsValid(Second) || !Second->IsNavigationActive()) continue;
			const bool bFirstYielding = First->SafetyInterlockStatus.Contains(TEXT("통행 양보 정지"));
			const bool bSecondYielding = Second->SafetyInterlockStatus.Contains(TEXT("통행 양보 정지"));
			const bool bClose = FVector::DistSquared2D(First->GetActorLocation(), Second->GetActorLocation()) < FMath::Square(700.f);
			const uint32 Low = static_cast<uint32>(FMath::Min(First->GetFleetId(), Second->GetFleetId()));
			const uint32 High = static_cast<uint32>(FMath::Max(First->GetFleetId(), Second->GetFleetId()));
			const uint64 PairKey = (static_cast<uint64>(Low) << 32) | High;
			if (bClose && bFirstYielding && bSecondYielding)
			{
				float& BlockedFor = BlockedPairSeconds.FindOrAdd(PairKey);
				BlockedFor += DeltaSeconds;
				if (BlockedFor >= 4.f)
				{
					// Only the higher ID yields, preventing both vehicles from reversing together.
					AAutomatedForklift* Yielder = First->GetFleetId() > Second->GetFleetId() ? First : Second;
					if (Yielder->TryFleetDeadlockYield(TEXT("다중 지게차 통로 교착 해소")))
					{
						BlockedPairSeconds.Remove(PairKey);
					}
					else
					{
						Yielder->SafetyInterlockStatus = TEXT("교착 대기: 후진 공간이 확보되지 않아 안전 정지 유지");
						BlockedFor = 0.f;
					}
				}
			}
			else
			{
				BlockedPairSeconds.Remove(PairKey);
			}
		}
	}
}

void AForkliftFleetManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Forklifts.RemoveAll([](const TObjectPtr<AAutomatedForklift>& Forklift) { return !IsValid(Forklift); });
	if (!bFleetMoving) return;
	DeadlockCheckElapsed += DeltaSeconds;
	if (DeadlockCheckElapsed < 1.f) return;
	const float Elapsed = DeadlockCheckElapsed;
	DeadlockCheckElapsed = 0.f;
	UpdateDeadlockHandling(Elapsed);
}
