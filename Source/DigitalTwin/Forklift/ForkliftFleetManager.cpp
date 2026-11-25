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
	if (!IsValid(Forklift) || AllForklifts.Contains(Forklift)) return;
	AllForklifts.Add(Forklift);
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
	// The body/tines extend forward from the actor origin. Keep the query above
	// the floor; an earlier 100 cm half-height intersected the floor and rejected
	// every candidate as occupied.
	const FVector QueryLocation = Location + FVector(0.f, 0.f, 8.f);
	const FCollisionShape Footprint = FCollisionShape::MakeBox(FVector(275.f, 105.f, 42.f));
	if (!GetWorld()->OverlapMultiByObjectType(Overlaps, QueryLocation, IgnoreForklift ? IgnoreForklift->GetActorQuat() : FQuat::Identity, ObjectTypes, Footprint, Query)) return true;
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
	if (PatrolForklifts.Num() >= PatrolFleetSize) return true;
	const FVector Origin = LeadForklift->GetActorLocation();
	const FRotator Rotation = LeadForklift->GetActorRotation();
	TArray<FVector> SpawnLocations;
	const FVector Right = LeadForklift->GetActorRightVector();
	const FVector Forward = LeadForklift->GetActorForwardVector();
	const float CandidateRadii[] = {700.f, 1050.f, 1450.f, 1900.f, 2400.f, 3000.f, 3700.f, 4500.f};
	for (const float Radius : CandidateRadii)
	{
		for (int32 Direction = 0; Direction < 16 && SpawnLocations.Num() < PatrolFleetSize; ++Direction)
		{
			const float Angle = 2.f * UE_PI * static_cast<float>(Direction) / 16.f;
			const FVector Offset = (Right * FMath::Cos(Angle) + Forward * FMath::Sin(Angle)) * Radius;
			const FVector Candidate = Origin + Offset;
			if (!IsSpawnAreaClear(Candidate, LeadForklift)) continue;
			bool bSeparatedFromPlanned = true;
			for (const FVector& Planned : SpawnLocations)
			{
				if (FVector::DistSquared2D(Candidate, Planned) < FMath::Square(650.f))
				{
					bSeparatedFromPlanned = false;
					break;
				}
			}
			if (bSeparatedFromPlanned) SpawnLocations.Add(Candidate);
		}
		if (SpawnLocations.Num() >= PatrolFleetSize) break;
	}
	if (SpawnLocations.Num() != PatrolFleetSize)
	{
		UE_LOG(LogTemp, Error, TEXT("[FORKLIFT_FLEET] Could not find 3 clear patrol spawn locations; found %d. Player forklift was left unchanged."), SpawnLocations.Num());
		return false;
	}

	TArray<AAutomatedForklift*> Spawned;
	for (const FVector& Candidate : SpawnLocations)
	{
		FActorSpawnParameters SpawnParams;
		// Candidate boxes above have already passed a conservative world overlap
		// query. Let UE resolve tiny contact-at-floor tolerances during spawn.
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AAutomatedForklift* NewForklift = GetWorld()->SpawnActor<AAutomatedForklift>(
			AAutomatedForklift::StaticClass(), Candidate, Rotation, SpawnParams);
		if (!IsValid(NewForklift))
		{
			for (AAutomatedForklift* Partial : Spawned) if (IsValid(Partial)) Partial->Destroy();
			UE_LOG(LogTemp, Error, TEXT("[FORKLIFT_FLEET] A planned patrol spawn collided; removed partial deployment."));
			return false;
		}
		RegisterForklift(NewForklift);
		PatrolForklifts.Add(NewForklift);
		Spawned.Add(NewForklift);
	}
	UE_LOG(LogTemp, Display, TEXT("[FORKLIFT_FLEET] Deployed exactly %d separate patrol forklifts; player forklift remains untouched."), Spawned.Num());
	return Spawned.Num() == PatrolFleetSize;
}

void AForkliftFleetManager::ToggleFleetOperation(AAutomatedForklift* RequestingForklift)
{
	if (!IsValid(RequestingForklift)) return;
	RegisterForklift(RequestingForklift);
	if (PatrolForklifts.Num() < PatrolFleetSize && !DeployFleet(RequestingForklift))
	{
		RequestingForklift->SetFleetOperationMessage(TEXT("순찰 지게차 3대를 배치할 공간을 찾지 못했습니다"));
		return;
	}

	bFleetMoving = !bFleetMoving;
	for (AAutomatedForklift* Forklift : PatrolForklifts)
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
	UE_LOG(LogTemp, Display, TEXT("[FORKLIFT_FLEET] Three patrol vehicles %s; player forklift unaffected."), bFleetMoving ? TEXT("started") : TEXT("stopped"));
}

void AForkliftFleetManager::UpdateDeadlockHandling(float DeltaSeconds)
{
	for (int32 A = 0; A < PatrolForklifts.Num(); ++A)
	{
		AAutomatedForklift* First = PatrolForklifts[A];
		if (!IsValid(First) || !First->IsNavigationActive()) continue;
		for (int32 B = A + 1; B < PatrolForklifts.Num(); ++B)
		{
			AAutomatedForklift* Second = PatrolForklifts[B];
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
	AllForklifts.RemoveAll([](const TObjectPtr<AAutomatedForklift>& Forklift) { return !IsValid(Forklift); });
	PatrolForklifts.RemoveAll([](const TObjectPtr<AAutomatedForklift>& Forklift) { return !IsValid(Forklift); });
	if (!bFleetMoving) return;
	DeadlockCheckElapsed += DeltaSeconds;
	if (DeadlockCheckElapsed < 1.f) return;
	const float Elapsed = DeadlockCheckElapsed;
	DeadlockCheckElapsed = 0.f;
	UpdateDeadlockHandling(Elapsed);
}
